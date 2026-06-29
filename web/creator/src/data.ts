// Creator tabs + the discrete slider option-sets. Counts come from the AvatarPreset
// registry (per gender): HairStyle 52, HairColor/EyebrowColor 32, EyeColor 25, SkinColor
// 24, EyebrowShape 20, HeadStyle(face) 15, FaceMarking sets ~13-16. A few (glasses,
// freckles/scars exact split) are best-guess until the index->preset catalog is wired on
// the C++ side; they're easy to tweak here.

export interface Section {
  key: string; // category id sent to the host as cc:option { category, index }
  label: string;
  max: number; // number of discrete options (slider goes 1..max)
}

export interface Tab {
  id: string;
  title: string;
  numeral: string;
  finalise?: boolean;
  sections: Section[];
}

export const TABS: Tab[] = [
  { id: "presets", title: "Presets", numeral: "I", sections: [{ key: "preset", label: "Preset", max: 30 }] },
  {
    id: "facewear",
    title: "Facewear",
    numeral: "II",
    sections: [
      { key: "faceShape", label: "Face Shape", max: 15 },
      { key: "eyeColour", label: "Eye Colour", max: 25 },
      { key: "glasses", label: "Glasses", max: 8 },
    ],
  },
  {
    id: "hair",
    title: "Hairstyles",
    numeral: "III",
    sections: [
      { key: "hairStyle", label: "Hair Style", max: 52 },
      { key: "hairColour", label: "Hair Colour", max: 32 },
    ],
  },
  {
    id: "complexion",
    title: "Complexion",
    numeral: "IV",
    sections: [
      { key: "skin", label: "Skin Tone", max: 24 },
      { key: "freckles", label: "Freckles", max: 16 },
      { key: "scars", label: "Scars", max: 13 },
    ],
  },
  {
    id: "eyebrows",
    title: "Eyebrows",
    numeral: "V",
    sections: [
      { key: "browShape", label: "Brow Shape", max: 20 },
      { key: "browColour", label: "Brow Colour", max: 32 },
    ],
  },
  { id: "finalise", title: "Finalise", numeral: "VI", finalise: true, sections: [] },
];

// Voice/pitch on the Finalise tab. Pitch range is a placeholder (the design assumed 9) —
// the real range comes from the in-game AvaAudio::SetPlayerVoicePitch probe.
export const VOICE_TONES = 2;
export const PITCH_MAX = 9;
