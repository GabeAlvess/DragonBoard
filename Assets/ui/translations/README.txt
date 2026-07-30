DragonBoardVR translation catalogs

Add a UTF-8 JSON file to this folder. No DLL rebuild is required.

Required structure:
{
  "language": "ja",
  "name": "日本語",
  "translations": {
    "English source text": "Translated text"
  }
}

Optional aliases can be used for INI values:
  "aliases": ["japanese", "jp"]

The language field is the value saved as Interface/sLanguage. The name field
is shown in the Settings language selector. Missing translation keys fall back
to their original English text. Languages are sorted by code, with English
always first. A language that needs glyphs not covered by the packaged fonts
also requires a compatible font to be registered by DragonBoardVR.
