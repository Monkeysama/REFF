#pragma once
#include "protocol.hpp"
#include <map>

namespace reff {
// UI 线程把 Win32 键位转换为标准 DOM key/code；保留物理位置供快捷键绑定，文字仍由 char/IME 提交。
inline void webview2_key_identity(Json& args,int key,int native,int flags){
    const bool extended=(native&(1<<24))!=0;
    std::string code,name;
    if(key>='A'&&key<='Z'){code="Key"+std::string(1,char(key));name=std::string(1,char(key+((flags&2)?0:32)));}
    else if(key>='0'&&key<='9'){code="Digit"+std::string(1,char(key));name=std::string(1,char(key));}
    else if(key>=VK_F1&&key<=VK_F24)code=name="F"+std::to_string(key-VK_F1+1);
    else if(key>=VK_NUMPAD0&&key<=VK_NUMPAD9){code="Numpad"+std::to_string(key-VK_NUMPAD0);name=std::to_string(key-VK_NUMPAD0);}
    else {
        const std::map<int,std::pair<std::string,std::string>> keys={
            {VK_BACK,{"Backspace","Backspace"}},{VK_TAB,{"Tab","Tab"}},{VK_RETURN,{extended?"NumpadEnter":"Enter","Enter"}},
            {VK_ESCAPE,{"Escape","Escape"}},{VK_SPACE,{"Space"," "}},{VK_PRIOR,{"PageUp","PageUp"}},{VK_NEXT,{"PageDown","PageDown"}},
            {VK_END,{"End","End"}},{VK_HOME,{"Home","Home"}},{VK_LEFT,{"ArrowLeft","ArrowLeft"}},{VK_UP,{"ArrowUp","ArrowUp"}},
            {VK_RIGHT,{"ArrowRight","ArrowRight"}},{VK_DOWN,{"ArrowDown","ArrowDown"}},{VK_INSERT,{"Insert","Insert"}},{VK_DELETE,{"Delete","Delete"}},
            {VK_SHIFT,{((native>>16)&255)==0x36?"ShiftRight":"ShiftLeft","Shift"}},
            {VK_CONTROL,{extended?"ControlRight":"ControlLeft","Control"}},{VK_MENU,{extended?"AltRight":"AltLeft","Alt"}},
            {VK_LWIN,{"MetaLeft","Meta"}},{VK_RWIN,{"MetaRight","Meta"}},{VK_APPS,{"ContextMenu","ContextMenu"}},
            {VK_CAPITAL,{"CapsLock","CapsLock"}},{VK_NUMLOCK,{"NumLock","NumLock"}},{VK_SCROLL,{"ScrollLock","ScrollLock"}},
            {VK_PAUSE,{"Pause","Pause"}},{VK_SNAPSHOT,{"PrintScreen","PrintScreen"}},
            {VK_MULTIPLY,{"NumpadMultiply","*"}},{VK_ADD,{"NumpadAdd","+"}},{VK_SUBTRACT,{"NumpadSubtract","-"}},
            {VK_DECIMAL,{"NumpadDecimal","."}},{VK_DIVIDE,{"NumpadDivide","/"}},
            {VK_OEM_1,{"Semicolon",";"}},{VK_OEM_PLUS,{"Equal","="}},{VK_OEM_COMMA,{"Comma",","}},
            {VK_OEM_MINUS,{"Minus","-"}},{VK_OEM_PERIOD,{"Period","."}},{VK_OEM_2,{"Slash","/"}},
            {VK_OEM_3,{"Backquote","`"}},{VK_OEM_4,{"BracketLeft","["}},{VK_OEM_5,{"Backslash","\\"}},
            {VK_OEM_6,{"BracketRight","]"}},{VK_OEM_7,{"Quote","'"}},{VK_OEM_102,{"IntlBackslash","\\"}}
        };
        if(auto it=keys.find(key);it!=keys.end()){code=it->second.first;name=it->second.second;}
    }
    if(!code.empty()){args["code"]=code;args["key"]=name;}
    args["autoRepeat"]=(native&(1<<30))!=0;
}
}
