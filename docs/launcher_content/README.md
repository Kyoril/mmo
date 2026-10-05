# Publishing launcher content

The Windows launcher reads `https://patch.mmo-dev.net/launcher/launcher.json` on each normal launch. It uses the native renderer and an independent background worker. No browser or backend service is needed.

Publish the supplied JSON under `/launcher/launcher.json` and these PNGs under `/launcher/images/`:

| Local source | Published filename |
|---|---|
| `src/launcher/res/splash.png` | `hero-v1.png` |
| `src/launcher/res/news_development.png` | `news-development-v1.png` |
| `src/launcher/res/news_world.png` | `news-world-v1.png` |

The supplied manifest keeps the approved layout and current artwork. Edit articles and patch notes directly. Lists are newest first. Bodies support paragraphs, `##` headings and `-` bullets; they are plain text, with no HTML, scripts or executable links. Upload images first and replace `launcher.json` last. When changing an image, use a new URL such as `hero-v2.png`, then update the manifest. Existing image URLs are immutable: cached files are reused without downloading them again.

The cache lives under `%LOCALAPPDATA%/AlestiaOnline/Launcher/content/`, separated by manifest URL. A valid cached manifest and its images appear immediately at startup. A failed manifest fetch retains that snapshot. Missing or invalid images use embedded artwork. A first offline launch uses the embedded content. Content failures never change updater status or Play availability. The cache is best effort; a read-only or full disk still allows fetched content to be displayed for the session.

`--content-url https://other-host/launcher.json` changes the source. `--no-content` disables remote content. The existing `--preview` and `--render-preview` modes do not contact either server. Remote content does not alter the download footer or window skin.

Format version must be integer `1`. Each news or patch entry needs `title`, `summary`, and `body`; `date` and `image` are optional. An article image appears on news cards and as a banner in its reader, including patch notes. `hero` accepts `image`, `title`, and `subtitle`. Image URLs must be absolute HTTPS URLs. Omitted or empty article lists use the built-in/local-file fallback for that list. Only PNG artwork is supported.

Limits: 256 KiB manifest, 32 news and 32 patch entries, 180-byte titles, 80-byte dates, 400-byte summaries, 64 KiB bodies. At most eight distinct images are loaded (hero first, then news, then patch notes), with 8 MiB compressed data per image, a 4096-pixel edge limit and a shared 8-megapixel decoded budget. Unused image files are removed after a successful manifest refresh. Five-second network inactivity limits and cancellation keep content work independent of launching or closing the game.

This feature targets the redesigned native Windows launcher. The original macOS Cocoa launcher remains unchanged.
