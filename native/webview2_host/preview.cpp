#include "runtime.hpp"
#include <cstring>
#include <winrt/base.h>
using Microsoft::WRL::Callback;
namespace reff {
// 坐标只接受绑定 iframe 的注册身份；Shell 可投影当前 iframe，但不能选择提供者插件身份。
bool WebViewRuntime::preview_query(const std::shared_ptr<Document>& doc,const Json& request,const std::shared_ptr<HostServices::Reply>& reply){
    const auto method=request.value("method",std::string{});if(!method.starts_with("ui.preview."))return false;
    auto fail=[&](const char* code){if(self_test&&send)send({{"type","test_diagnostic"},{"previewError",code},{"method",method}});reply->Failure(400,Json{{"code",code},{"message",code}}.dump());return true;};
    auto success=[&](Json value){reply->Success(value.dump());return true;};
    if(method=="ui.preview.capabilities")return success({{"version",1},{"layoutProtocol",2},{"maxSlots",preview_slots},{"composition","underlay-alpha"},{"color","sdr-unorm8"}});
    const auto& params=request["params"];
    if(!ready_.value("ready",false))return fail("NOT_READY");
    if(method=="ui.preview.register"||method=="ui.preview.remove"){
        if(doc->main)return fail("FORBIDDEN");
        if(!params.contains("slotId")||!params["slotId"].is_string())return fail("INVALID_ARGUMENT");
        const auto name=params["slotId"].get<std::string>();if(!preview_name(name,64))return fail("INVALID_ARGUMENT");
        if(method=="ui.preview.remove"){doc->preview_ids.erase(name);clear_previews();return success({});}
        if(doc->preview_ids.size()>=preview_slots&&!doc->preview_ids.contains(name))return fail("SLOT_LIMIT");
        if(doc->preview_ids.insert(name).second)publish_previews();return success({{"pageId",doc->services.page_identity().page_id}});
    }
    if(method!="ui.preview.present"||!doc->main)return fail("FORBIDDEN");
    if(!params.contains("slots")||!params["slots"].is_array()||params["slots"].size()>preview_slots||
        !params.contains("pageId")||!params["pageId"].is_string()||
        !params.contains("viewportWidth")||!params["viewportWidth"].is_number_integer()||!params.contains("viewportHeight")||!params["viewportHeight"].is_number_integer()||
        params.value("viewportWidth",0)!=preview_width_||params.value("viewportHeight",0)!=preview_height_)return fail("INVALID_ARGUMENT");
    std::shared_ptr<Document> owner;
    for(const auto& candidate:documents_)if(!candidate->main&&!candidate->dead&&!candidate->ticket.empty()&&
        candidate->services.page_identity().page_id==params["pageId"])owner=candidate;
    if(!owner)return fail("PAGE_UNLOADED");
    const auto route=owner->services.route_for_url(owner->url);if(!route||route->shell||!preview_name(route->manifest->id,96))return fail("FORBIDDEN");
    if(!params.contains("paint")||!params["paint"].is_array()||params["paint"].size()!=3||
        !params["paint"][0].is_number()||!params["paint"][1].is_number()||!params["paint"][2].is_number_unsigned()||
        !params.contains("layoutToken")||!params["layoutToken"].is_number_unsigned())return fail("INVALID_ARGUMENT");
    const float px=params["paint"][0].get<float>(),py=params["paint"][1].get<float>();
    const auto token=params["paint"][2].get<std::uint64_t>(),layout=params["layoutToken"].get<std::uint64_t>();
    if(!std::isfinite(px)||!std::isfinite(py)||std::abs(px)>32768||std::abs(py)>32768||token>0xffffff||!layout||layout>0xffffff)return fail("INVALID_ARGUMENT");
    PreviewFrame next{};next.revision=preview_.revision;next.flags=preview_.flags;
    for(const auto& item:params["slots"]){
        if(!item.is_object()||!item.contains("slotId")||!item["slotId"].is_string())return fail("INVALID_ARGUMENT");
        const auto id=item["slotId"].get<std::string>();if(!owner->preview_ids.contains(id))return fail("SLOT_NOT_OWNED");
        auto rectangle=[](const Json& value,PreviewRect& out){
            if(!value.is_array()||value.size()!=4)return false;float numbers[4]{};
            for(int i=0;i<4;++i){if(!value[i].is_number())return false;numbers[i]=value[i].get<float>();if(!std::isfinite(numbers[i])||std::abs(numbers[i])>32768)return false;}
            out={numbers[0],numbers[1],numbers[2],numbers[3]};return out.width>0&&out.height>0;
        };
        PreviewSlot slot{};if(!item.contains("rect")||!item.contains("clip")||!rectangle(item["rect"],slot.destination)||!rectangle(item["clip"],slot.clip))return fail("INVALID_ARGUMENT");
        slot.clip=intersect_preview(intersect_preview(slot.clip,slot.destination),{0,0,float(preview_width_),float(preview_height_)});
        if(slot.clip.width<=0||slot.clip.height<=0)continue;
        for(unsigned i=0;i<next.count;++i){const auto overlap=intersect_preview(slot.clip,next.slots[i].clip);
            if(overlap.width>0&&overlap.height>0){clear_previews();return fail("OVERLAPPING_SLOTS");}}
        std::strcpy(slot.plugin,route->manifest->id.c_str());std::strcpy(slot.id,id.c_str());next.slots[next.count++]=slot;
    }
    if(std::memcmp(&next,&preview_,sizeof(next))!=0||px!=preview_paint_x_||py!=preview_paint_y_||token!=preview_paint_token_||layout!=preview_layout_token_){
        preview_paint_x_=px;preview_paint_y_=py;preview_paint_token_=std::uint32_t(token);preview_layout_token_=std::uint32_t(layout);preview_=next;publish_previews();}
    return success({{"revision",preview_.revision}});
}
// 先通知 Core 撤销旧槽位，再把修订标记画进同一浏览器帧；旧帧无权携带新位置。
void WebViewRuntime::publish_previews(){
    preview_.flags=0;for(const auto& doc:documents_)if(!doc->dead&&!doc->preview_ids.empty())preview_.flags|=preview_surface_active;
    ++preview_.revision;
    if(self_test&&send)send({{"type","test_diagnostic"},{"previewRevision",preview_.revision},{"previewCount",preview_.count},{"previewWidth",preview_width_},{"previewHeight",preview_height_},{"previewGeneration",preview_generation_}});
    if(send&&!send({{"type","preview_state"},{"revision",preview_.revision}})){preview_.count=0;return;}
    if(!web_)return;
    const auto n=preview_.revision&0xffffffu;
    const auto script=widen("(()=>{let e=document.getElementById('reff-preview-stamp');if(!e){e=document.createElement('i');e.id='reff-preview-stamp';document.documentElement.append(e);}e.style.cssText='position:fixed!important;left:100px!important;top:22px!important;width:1px!important;height:1px!important;z-index:2147483647!important;pointer-events:none!important;opacity:1!important;background:rgb("+
        std::to_string(n&255)+","+std::to_string((n>>8)&255)+","+std::to_string((n>>16)&255)+")!important';})()");
    web_->ExecuteScript(script.c_str(),nullptr);
}
void WebViewRuntime::clear_previews(){preview_.count=0;preview_.slots={};publish_previews();}
void WebViewRuntime::viewport(int width,int height,std::uint64_t generation){
    if(width==preview_width_&&height==preview_height_&&generation==preview_generation_)return;
    preview_width_=width;preview_height_=height;preview_generation_=generation;clear_previews();
}
PreviewFrame WebViewRuntime::preview_frame(const unsigned char* pixels,unsigned stride,int width,int height,std::uint64_t generation){
    PreviewFrame empty{};empty.revision=preview_.revision;empty.flags=preview_.flags|(captured_preview_surface_?preview_surface_active:0);
    if(!ready_.value("ready",false)||width!=preview_width_||height!=preview_height_||generation!=preview_generation_)return empty;
    const auto* stamp=pixels+22*stride+100*4;
    const auto revision=uint32_t(stamp[2])|(uint32_t(stamp[1])<<8)|(uint32_t(stamp[0])<<16);
    if(stamp[3]!=255||revision!=(preview_.revision&0xffffffu))return empty;
    // 同时验证子页透明孔布局、Shell 投影布局及宿主授权修订，避免跨文档 paint 先后造成旧孔位放行。
    const auto matches=[&](float x,float y,std::uint32_t token){
        const int ix=int(std::floor(x)),iy=int(std::floor(y));if(ix<0||iy<0||ix>=width||iy>=height)return false;
        const auto* p=pixels+iy*stride+ix*4;return p[3]==255&&(uint32_t(p[2])|(uint32_t(p[1])<<8)|(uint32_t(p[0])<<16))==token;
    };
    if(preview_.count&&(!matches(105,23,preview_layout_token_)||!matches(preview_paint_x_,preview_paint_y_,preview_paint_token_)))return empty;
    captured_preview_surface_=(preview_.flags&preview_surface_active)!=0;return preview_;
}
}
