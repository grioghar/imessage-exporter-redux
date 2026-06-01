# Connecting Google Contacts

iMessage Exporter can download your Google Contacts (read-only) and use them to
resolve phone numbers / emails to names. Because Google requires every app to
use its **own** OAuth credentials, you create a free OAuth client once and paste
it into the app (**Connect Google Contacts…** button, or it prompts you the
first time). The app also reads `IMSG_GOOGLE_CLIENT_ID` / `IMSG_GOOGLE_CLIENT_SECRET`
from the environment if you prefer.

## One-time setup (about 5 minutes)

1. **Project** — open the [Google Cloud Console](https://console.cloud.google.com/)
   and create or select a project.
2. **Enable the API** — **APIs & Services → Library**, search **People API**,
   click **Enable**.
3. **OAuth consent screen** — **APIs & Services → OAuth consent screen**:
   - User type **External**, then fill the app name and your support email.
   - Add the scope **`https://www.googleapis.com/auth/contacts.readonly`**.
   - Under **Test users**, add the Google account whose contacts you'll export.
     (While the app is in "Testing" you don't need Google's verification; test
     users can sign in immediately.)
4. **Create the client** — **APIs & Services → Credentials → Create Credentials →
   OAuth client ID**, Application type **Desktop app**. Name it anything.
5. **Copy** the generated **Client ID** and **Client secret**.

## Using it in iMessage Exporter

1. Click **Connect Google Contacts…**.
2. Paste the **Client ID** and **Client secret** and click **Connect**.
3. Your browser opens Google's sign-in; approve the read-only contacts access.
4. Contacts are downloaded into the app's persistent contacts database and used
   for name resolution (select **"Saved contacts database"** under Names).

## Privacy / security notes

- The **client secret is stored in your OS keychain** (macOS Keychain / Windows
  Credential Manager; a 0600 file on Linux), not in plain config.
- The OAuth **refresh token** is also kept in the keychain.
- The app requests **`contacts.readonly`** only — it cannot modify your contacts,
  and data goes only between your machine and Google.
- For a "Desktop app" client the "secret" is not truly confidential (per Google's
  own docs); it's still kept out of plain sight.
