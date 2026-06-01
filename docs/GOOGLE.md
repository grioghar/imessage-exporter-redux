# Connecting Google Contacts

iMessage Exporter can download your Google Contacts (read-only) via Google's
**People API** and use them to resolve phone numbers / emails to names. Google
requires every app to use its **own** OAuth credentials, so you create a free
OAuth client once and paste it into **Connect Google Contacts…** (it also reads
`IMSG_GOOGLE_CLIENT_ID` / `IMSG_GOOGLE_CLIENT_SECRET` from the environment).

> Google moved OAuth configuration into the **Google Auth Platform** (the old
> "OAuth consent screen" page now redirects there). The steps below match the
> current console (as of 2025–2026); exact labels may shift, but the pieces are
> the same: enable the API, set up the auth platform (branding + audience +
> scope), create a Desktop client.

## One-time setup (~5 minutes)

1. **Project** — open the [Google Cloud Console](https://console.cloud.google.com/)
   and create or select a project.
2. **Enable the API** — **APIs & Services → Library**, search **People API**,
   **Enable**.
3. **Google Auth Platform** — go to **APIs & Services → OAuth consent screen**
   (this opens *Google Auth Platform*). If prompted, click **Get started**, then:
   - **Branding:** app name, your user-support email, developer contact email.
   - **Audience:** User type **External**; leave **Publishing status = Testing**;
     under **Test users**, **Add users** and enter the Google account whose
     contacts you'll export. (Test users can sign in immediately with no Google
     verification.)
   - **Data access:** **Add or remove scopes** → add
     **`https://www.googleapis.com/auth/contacts.readonly`** → Update/Save. To
     also upload exports to Drive, add **`https://www.googleapis.com/auth/drive.file`**
     (the app can only touch files it creates — see *Google Drive upload* below).
4. **Create the client** — **Clients → Create client** (a.k.a. Credentials →
   Create credentials → OAuth client ID), Application type **Desktop app**, name
   it anything, **Create**.
5. **Download JSON** (the ⬇ button next to the client) — or just copy the
   **Client ID** and **Client secret** from the dialog.

## Use it in the app

1. Click **Connect Google Contacts…**. Either click **Import client JSON…** and
   pick the file you downloaded, or paste the **Client ID** + **Client secret**.
   Leave **Save credentials (encrypted in the OS keychain)** ticked to remember
   them (stored encrypted, never in plaintext settings); untick it to use them
   only for this run.
2. Click **Continue/Connect** and approve the read-only contacts access in the
   browser window that opens (the app listens on a local `http://127.0.0.1`
   loopback redirect — the modern Desktop-app flow; the old "out-of-band/OOB"
   copy-paste code flow is retired).
3. Contacts download into the persistent contacts database; pick **"Saved
   contacts database"** under **Names** to use them.

## Google Drive upload

Send each export straight to your Drive:

1. Make sure the **`drive.file`** scope is added (setup step 3) and you've set up
   the OAuth client (above).
2. Click **Connect Google Drive…** and approve access in the browser. The
   authorization (a refresh token) is **saved encrypted in your OS keychain**, so
   you only do this once — future exports upload without signing in again.
3. Enter a **Drive folder name** (e.g. `iMessage Export`) and tick **Upload
   export to Drive when finished**.
4. Run an export. When it completes, the whole output folder — files **and** the
   per-conversation attachment subfolders — is uploaded into that Drive folder
   (created if it doesn't exist). PDF exports upload the generated PDFs.

`drive.file` is **least-privilege**: the app can only see and manage the files it
creates, never the rest of your Drive. To revoke, remove the app at
[myaccount.google.com → Security → Third-party access](https://myaccount.google.com/connections).

## Important: the 7‑day Testing limit (and why we keep it in Testing)

`contacts.readonly` is a **sensitive scope**. If you "Publish" the app to
**Production**, Google requires an app-verification review. To avoid that for
personal use, keep the app in **Testing** and add yourself as a **test user**.

The trade-off: for an app in **Testing**, Google **expires the refresh token
after 7 days**. That doesn't block one-off exports — each time you click
**Connect Google Contacts…** it re-authorizes and re-downloads — you just sign
in again if it's been more than a week. (If you ever need long-lived background
refresh, you'd publish + verify the app.)

## Privacy / security

- **Client secret** and the **refresh token** are stored in your OS keychain
  (macOS Keychain / Windows Credential Manager; a `0600` file on Linux), not in
  plain config.
- Only **`contacts.readonly`** is requested — the app can't modify your contacts,
  and data flows only between your machine and Google.
- For a "Desktop app" client the secret isn't truly confidential (per Google's
  docs); it's still kept out of plain sight.
