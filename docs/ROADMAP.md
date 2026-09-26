# Roadmap

The 0.6.0-0.6.1 cycle shipped themes, statistics, inactive-contact filtering,
per-conversation stats, an interactive timeline page, SMS/RCS green-text style,
and the granular Statistics and Timeline preferences tabs. Below is the planned
scope for 0.7.0:

- **AI relationship analysis engine.** Analyse the patterns and dynamics within
  conversations — sentiment arcs, topic clustering, response-time graphs, dominant
  speakers, relationship strength over time, and statistical correlations between
  contacts and time/date/context. Produces a rich analytical report alongside the
  export.
- **AI service OAuth + message summarisation.** An OAuth sign-in flow for major
  AI providers (OpenAI/ChatGPT, Anthropic/Claude, Google Gemini, and others).
  Once authenticated, selected conversations or date ranges can be submitted for
  AI-generated summary, sentiment analysis, or Q&A. Credentials are stored
  encrypted in the platform keychain alongside existing Google credentials.
- **Reply engine (HTML → iMessage).** A way to reply to conversations directly
  from the exported HTML page. Each message bubble gains a reply affordance that
  opens a compose area; sending routes through the macOS Messages URL scheme
  (or AppleScript on macOS) to deliver the reply via iMessage without leaving
  the export.
- **Low-footprint backup streaming.** For large archives (hundreds of GB on an
  external drive or iPhone backup), process and export one conversation at a time
  directly from the backup without ever extracting the full `sms.db` to the local
  machine. The export engine already streams one conversation at a time in memory;
  this extends that to the source side — read a conversation's rows from the
  backup's SQLite blob, write the HTML, move on. Lets you export a 250 GB message
  history to an external drive without the laptop ever holding more than one
  conversation's worth of data.
- **Location context on the timeline.** Correlate message timestamps with location
  records from three sources the tool can already reach: the macOS Photos library
  (Photos.sqlite, same Full Disk Access permission already required), the
  com.apple.routined/Local.sqlite significant-location database inside an iPhone
  backup (which the tool already parses), and a Google Takeout Records.json export
  (parseable with the Google OAuth infrastructure already built). The result: each
  message on the timeline can optionally show where you were when you sent it.
  Waze and the real-time Google Maps Timeline API have no public endpoint; the
  Takeout JSON is the only Google export path.
- **Media compression and cloud offload** (via [grioghar/lightpress](https://github.com/grioghar/lightpress) + [grioghar/ffmpeg](https://github.com/grioghar/ffmpeg)).
  A new sister library — `lightpress` — provides a zero-dependency C++17 JPEG/PNG
  encoder, EXIF metadata stripper, bilinear/bicubic resize, and MP4/MOV container
  rewriter that removes thumbnail tracks and cover-art atoms without re-encoding the
  video bitstream. Its benchmark suite compares output size and SSIM quality against
  ffmpeg baselines on CI. Integration into the exporter gives two compression modes:
  (1) *local transcode* — re-encode images at a configurable quality level and strip
  all EXIF metadata, typically achieving 30–60% size reduction; strip MP4 metadata
  atoms for immediate gains without touching the video bitstream; for full video
  transcoding, optionally shell out to `ffmpeg` when present; (2) *cloud offload* —
  upload attachments to Google Drive (using the existing Drive connector) and rewrite
  `src=` paths to Drive URLs, keeping the HTML self-contained and lightweight.
  Both modes are opt-in and combinable.
- **Encryption-at-rest with self-decrypting exports.** Protect sensitive exports
  with a password. The implementation uses AES-256-GCM with a PBKDF2-derived key:
  for HTML exports the ciphertext is embedded directly in the file alongside an
  inline Web Crypto API decryption loop — open in any modern browser, enter your
  key, it decrypts in memory and renders, with no server, plugin, or special app
  required. JSON and TXT exports write a standard encrypted container. Keys are
  never stored; all cryptography runs in the browser or in the native binary.
- **Conversation replay / message player.** A "play back this conversation" mode
  embedded in each exported HTML file: messages appear one at a time in
  chronological order as animated bubbles, with a speed control (1× to 20×) and a
  scrubber to jump to any point. If location data is available (from the location
  context feature), a mini-map in the corner pans and zooms to where each party
  was when they sent their message. Pure HTML/CSS/JS, no server required.
- **Extended statistics — unique, unexpected, and fun.** A greatly expanded stats
  engine covering relationships and patterns that no other export tool surfaces:
  - *Conversation dynamics:* who initiates more often; who ends conversations; the
    "ghost ratio" (how often one side stops replying); double-texter frequency;
    fastest-ever response time (to the second); average response time by hour of day.
  - *Language fingerprints:* word frequency and phrase clouds per person; emoji
    personality profile (top 10 emoji per sender); exclamation-point density;
    question-asker vs answer-giver ratio; longest message ever sent ("The Novel");
    total word count across all history ("N books' worth of conversation").
  - *Temporal oddities:* night-owl index (% of messages sent midnight–4 am);
    busiest single minute ever; the exact timestamp of the very first message to
    each person; "comeback conversations" (threads silent for 6+ months that
    restarted); seasonal activity — which month of the year you text most.
  - *Relationship arcs:* a heat-map of message frequency over the years per contact
    (fading friendships and growing ones visible at a glance); "dormant contacts"
    (daily texters who went quiet); who you've known the longest based on first
    message date.
  - *Silly but accurate:* how many times you sent "on my way" (and to whom); "I
    love you" count per relationship; most-used swear word (shown with partial
    censor); longest unbroken chain of single-word replies; the contact most likely
    to respond with just "ok"; your personal peak texting hour; and a "phone
    addict score" (% of replies sent within 60 seconds of receiving).

Suggestions and votes welcome — open an issue or add a 👍 to an existing one.
