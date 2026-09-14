import type { ReffAppearance, ReffLanguage } from '@reff/ui';

export type ReffSettings = {
  schemaVersion: 1;
  language: ReffLanguage;
  appearance: ReffAppearance;
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
  appearance?: Partial<ReffAppearance>;
  input?: Partial<ReffSettings['input']>;
  window?: Pick<Partial<ReffSettings['window']>, 'rememberGeometry'>;
};

export type LoadedPlugin = {
  id: string;
  name: string;
  version: string;
  mode: 'component' | 'isolated-page';
};
