import type { ReffAppearance, ReffLanguage } from '@reff/ui';

export type ReffSettings = {
  schemaVersion: 1;
  language: ReffLanguage;
  hotkey: { key: number; modifiers: number };
  appearance: ReffAppearance;
  startup: {
    preload: boolean;
  };
  input: {
    mousePassthrough: boolean;
    keyboardPassthrough: boolean;
  };
  window: {
    rememberGeometry: boolean;
    geometry?: {
      clientWidth: number;
      clientHeight: number;
      left: number;
      top: number;
      width: number;
      height: number;
    };
  };
};

export type SettingsPatch = {
  language?: ReffLanguage;
  hotkey?: Partial<ReffSettings['hotkey']>;
  appearance?: Partial<ReffAppearance>;
  startup?: Partial<ReffSettings['startup']>;
  input?: Partial<ReffSettings['input']>;
  window?: Pick<Partial<ReffSettings['window']>, 'rememberGeometry'>;
};

export type LoadedPlugin = {
  id: string;
  name: string;
  localizedName?: Partial<Record<ReffLanguage, string>>;
  author?: string;
  version: string;
  mode: 'component' | 'isolated-page';
};
