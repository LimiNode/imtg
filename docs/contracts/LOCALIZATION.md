# Localization contract

**Semantic owner:** localization maintainers. **Status:** contract-only.

Catalogs are immutable snapshots loaded from JSON or another documented source. A render resolves locale from explicit application policy (for example user preference, then chat/application default, then `en`) and holds one snapshot for its entire render/action response. There is no global mutable language context; users in one group may render different locales.

## Catalog shape

```json
{
  "locale": "ru",
  "fallback": "en",
  "messages": {
    "settings.title": "Настройки",
    "items.count": {
      "one": "{count} элемент",
      "few": "{count} элемента",
      "many": "{count} элементов",
      "other": "{count} элемента"
    }
  },
  "media": {
    "help.image": {"kind": "telegram_file", "value": "file-id"}
  }
}
```

Locale resolution is deterministic: explicit user locale, configured chat locale (if policy permits), application default, then `en`; each catalog may add its declared parent before the final default. A missing key searches that chain and finally returns a stable diagnostic placeholder or typed missing-resource result, according to the caller's strictness. Duplicate keys in one catalog are a load error, not last-write-wins. Malformed JSON, invalid locale tags, missing plural `other`, and invalid resource metadata reject the snapshot and leave the prior snapshot unchanged.

Named arguments are type-checked and escaped by the formatter. Missing or extra arguments are deterministic formatting errors; no user-provided string is interpreted as a template. Plural selection uses locale rules with `one`, `few`, `many`, `zero`, `two`, and `other` as supported categories; unknown categories fall back to `other`. Rules may evolve toward CLDR without changing stable message keys.

Text and media resources share locale resolution but have separate validation and caching. A snapshot is reference-counted/owned for the render lifetime and may be replaced atomically for future renders. Stable UI IDs, callback tokens, and FSM state names never use translated text.

**Verification:** concurrent users with different locales; fallback-chain and parent-locale tests; named-argument escaping and missing-argument tests; plural category tests; immutable snapshot lifetime tests; duplicate/malformed catalog tests; media-resource failure tests; and ID independence tests. **Dependencies:** UI/actions and sessions; durable user preferences are an optional service.
