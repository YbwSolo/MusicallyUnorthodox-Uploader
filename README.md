# MU Uploader

A plugin from Musically Unorthodox that posts your beats without leaving the DAW.

Put it last on the master. When you export the beat the plugin catches it, you give it a name, a price and a cover,
and it makes the video and posts it to your YouTube channel with the title, description and tags written for you.
It also saves every beat into catalogs, lays the files out for BeatStars, and writes the emails when you pitch a beat.

Works in FL Studio, Logic, Ableton and anything else that loads VST3 or AU plugins, on Mac and Windows.

## Install

Go to **Releases** on the right of this page and download the newest build.

- **Mac:** `MUUploader-Mac.pkg`. Double-click it and click through. The first time, macOS says it could not check
  the file: press Done, open System Settings > Privacy & Security, scroll to the bottom and press Open Anyway.
- **Windows:** `MUUploader-Windows-Setup.exe`. Close your DAW first. If Windows says it does not recognise the
  publisher, choose More info > Run anyway.

Then open your DAW, rescan plugins if it does not show up, and put **MU Uploader** in the last slot on the master.

## Using it

1. **Settings:** type your producer name, contact email, Instagram and BeatStars store link, and press Connect YouTube.
2. **Upload:** export your beat the way you always do. The plugin shows "Beat caught". (Or press Record and play the
   beat from the top.) Fill in the name, type beat artist, BPM, key, price and tags, and drop a cover on the square.
3. Pick the licence:
   - **Free for profit:** artists can release on every platform as long as they credit you and do not claim Content ID.
   - **Free:** free to record on, but they have to buy a lease to release it on streaming platforms.
4. Press **Upload**. It makes the video, posts it, and gives you the link. Choose Schedule to have it go public later.
5. **Beats** keeps everything you have saved. Make catalogs and sub-categories, and drag a beat out to get its file.
6. **Email** writes a submission email for the beat and opens it in your own mail app.
7. **Assistant:** type it in one go, for example
   `Name it Get It, Gunna type beat, 161 bpm, key E minor, $25, free for profit, post it tomorrow at 6pm`.

### BeatStars

BeatStars does not let apps upload beats for you. Tick **Pack for BeatStars** and the plugin puts the WAV, the MP3,
a square cover and the details in one folder, then opens BeatStars so you can drag them in. Paste the beat's
BeatStars link back into the Upload page and it becomes the buy link in the YouTube description.

## Where things are kept

- Your beats: `Music/MU Uploader`
- Settings, YouTube sign-in and contacts: the app data folder for your user (`MU Uploader`)

The plugin never changes your sound. Audio passes straight through.

## Building

Every push to `main` builds the Mac and Windows installers on GitHub Actions and publishes them as a release.
The YouTube sign-in needs a Google client ID and secret, stored as the repository secrets `MUU_GOOGLE_CLIENT_ID` and
`MUU_GOOGLE_CLIENT_SECRET` (or typed into Settings > Advanced on one computer).
