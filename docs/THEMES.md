# Authoring HTML themes

The HTML/PDF export ships with five built-in themes (`ios`, `lcars`, `matrix`,
`dot-matrix`, `atari`). Every theme reuses the **same** HTML structure and CSS
class names — only the stylesheet text changes — so a theme is just a recolour
of the shared layout. You can add your own without touching the engine: drop a
small JSON file describing the colours and a font, and it becomes selectable
exactly like a built-in.

This guide covers the JSON schema, where theme files live, how the colours map
onto the page, and how to select a theme.

## JSON schema

A theme is a **flat JSON object** whose values are all strings:

| Field         | Meaning                                              | Default if omitted |
|---------------|------------------------------------------------------|--------------------|
| `name`        | The theme's identifier (what you pass to `--theme`). **Required.** | — (file is rejected) |
| `bg`          | Page background. Any CSS colour (`#rrggbb`, `rgb(...)`, a named colour, or even a `linear-gradient(...)`). | unchanged from base (light grey) |
| `text`        | Default text colour. Also tints the muted meta/info/handle lines (at reduced opacity). | unchanged (near-black) |
| `bubble_me`   | Background of **your** sent bubbles (`.msg.me .bubble`). | unchanged (iOS blue) |
| `bubble_them` | Background of the **other** person's bubbles (`.msg.them .bubble`). | unchanged (iOS grey) |
| `accent`      | Highlight colour: the header title, contact names, and link-card titles/borders. | unchanged |
| `font`        | A CSS `font-family` stack, e.g. `"Georgia, serif"`. | system sans-serif |

Notes:

- Only `name` is required. Every other field is optional — a partial theme just
  leaves those parts of the base layout untouched, so you can override only what
  you care about.
- The parser is deliberately tiny and tolerant: surrounding/embedded whitespace
  is fine, and `\"` / `\\` escapes work inside strings. It only understands a
  flat object of string values — no nested objects, arrays, or numbers.
- Colour values are emitted into CSS verbatim. Stick to valid CSS colours (or
  gradients for `bg`); there is no validation, so a typo just yields a colour
  the browser ignores.

## Where theme files live

Put each theme in its own `*.json` file inside a **`themes/`** folder. The
application loads every `*.json` in that folder (non-recursively) at startup via
`load_themes_from_dir("themes")`; each file that parses and has a `name` becomes
available. A file whose `name` matches a built-in (e.g. `ios`) overrides that
built-in for the session.

```
themes/
├── sunset.json
├── midnight.json
└── newsprint.json
```

Programmatically, the same loaders are part of the SQLite-free core
(`include/imsg/theme.hpp`):

```cpp
imsg::load_theme_from_json(json_text);     // register one theme from a string
imsg::load_themes_from_dir("themes");       // register every *.json in a folder
// thereafter: imsg::is_theme("sunset"), imsg::theme_css("sunset"), etc.
```

## A worked example

`themes/sunset.json`:

```json
{
  "name": "sunset",
  "bg": "#1a0a2e",
  "text": "#fce8d8",
  "bubble_me": "#ff6b6b",
  "bubble_them": "#2a1a4a",
  "accent": "#ffd166",
  "font": "Georgia, serif"
}
```

Render with it:

```bash
./imessage-exporter --format html --theme sunset --output ./export
```

What each field does to the page:

- `bg` → the whole-page background (`body`), and the background of link/Open-Graph
  preview cards so they stay legible on the dark field.
- `text` → the default body text colour, plus the muted lines (the
  sender · time `.info`, the `header .meta` summary, and `.contact-handle`)
  rendered at ~70% opacity so they recede.
- `bubble_them` → the incoming bubbles (`.msg.them .bubble`); the bubble text
  uses `text`.
- `bubble_me` → your outgoing bubbles (`.msg.me .bubble`); the bubble text is
  white for contrast.
- `accent` → the `header h1` title, every `.contact-name`, link-card titles
  (`.linkcard-host` / `.ogcard-title`), and the link-card border.
- `font` → the `font-family` for the whole document.

The shared layout — bubble shapes, avatars, the per-conversation contact header,
attachment/embed rendering, and the page-break rules that stop messages
splitting across PDF pages — is inherited unchanged. Themes only ever change
paint, never layout.

> SMS/RCS conversations always tint **sent** bubbles iOS-green via the
> `.sms-style` rule in the base layout, so green-bubble threads stay
> recognisable regardless of `bubble_me`. Override `.sms-style .msg.me .bubble`
> in a hand-written CSS theme if you need to change that.

## Selecting a theme

- **CLI:** `--theme NAME`, e.g. `--theme sunset`. The default is `ios`. Built-in
  names plus any loaded from `themes/` are accepted; an unknown name falls back
  to `ios`.
- **Desktop GUI:** pick it from the theme menu. Loaded JSON themes appear
  alongside the built-ins.

## Copy-paste starter

Save as `themes/my-theme.json`, change `name`, and tweak the colours:

```json
{
  "name": "my-theme",
  "bg": "#101418",
  "text": "#e6e6e6",
  "bubble_me": "#2f81f7",
  "bubble_them": "#21262d",
  "accent": "#f0883e",
  "font": "-apple-system, 'Segoe UI', Helvetica, Arial, sans-serif"
}
```
