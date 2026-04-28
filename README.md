# Talteen
Talteen is your all-in-one backup companion for Sailfish OS. Whether you are switching devices or just playing it safe, Talteen bundles your most important data into a single, secure archive. Easily back up your messages, call history, photos, media, and even your app settings. Everything is locked behind strong encryption, giving you total peace of mind that your personal data stays yours.

Available on [OpenRepos](https://openrepos.net/content/fravaccaro/talteen-backup).

<a href="screenshots/grayscale.png"><img width="25%" style="float: left;" src="screenshots/grayscale.png" /></a> <a href="screenshots/brightness.png"><img width="25%" style="float: left;" src="screenshots/brightness.png" /></a>
<br style="clear: both; height:5px;" /> <a href="screenshots/contrast.png"><img width="25%" style="float: left;" src="screenshots/contrast.png" /></a>
<br style="clear: both; height:5px;" />

## Features
* Save your messages, call history, app data, app grid layout, and media.
* Easily move your data to a new device by copying the backup file via an SD card or over Wi-FI.
* Lock your archives with standard AES-256 encryption.
* Built on standard Linux formats (tar, openssl) so you can extract backups on any PC.

### Under the hood
Talteen acts as a frontend for:
* Archiving: `tar`
* Compression: `xz`
* Encryption: `openssl` (AES-256-CBC with PBKDF2)
* A custom `QTcpServer`/`QTcpSocket` implementation to facilitate device-to-device transfers over the local network.


## Archive structure
The app creates a standard `tar` archive with extension `.talteen` holding two files:
* `payload.enc`: An AES-256 encrypted, XZ-compressed stream containing the actual files and folders.
* `manifest.yaml`: a human-readable file containing the metadata necessary for the app to understand what is inside the encrypted payload without having to decrypt it first.

### Manual backup extraction
The backup files are saved to:
* Internal storage: `/home/defaultuser/.local/share/harbour-talteen`
* SD card: `/run/media/sdcard/harbour-talteen`

You can extract legacy `v1` (`AES-256-CBC`) backups on any Linux terminal without using the app:

1. Unpack the container:
   ```
   tar -xf backup_name.talteen
   ```
2. Decrypt and unpack the payload: 
   ```
   openssl enc -d -aes-256-cbc -pbkdf2 -in payload.enc | tar -xJv
   ```

### Manual v2 backup extraction
For `v2` backups (`encryption: "openssl-aes-256-gcm"`), use the helper script in this repo:

```
scripts/decrypt-v2.sh -i /path/to/backup.talteen -o /path/to/output
```

If `-p` is omitted, the script prompts for the password:

```
scripts/decrypt-v2.sh -i backup.talteen -o restored-data
```

You can optionally pass a password directly (less secure in shell history):

```
scripts/decrypt-v2.sh -i backup.talteen -o restored-data -p 'your-password'
```

The script validates v2 manifest fields (`kdf_iterations`, `salt_b64`, `iv_b64`, `tag_b64`, `aad`) and then decrypts with OpenSSL EVP AES-256-GCM before extracting the payload.



## Support
If you like my work and want to buy me a beer, [feel free to do it](https://www.paypal.me/fravaccaro)!

## Translate
Request a new language or contribute to existing languages on the [Transifex project page](https://explore.transifex.com/fravaccaro/talteen/).

## Credits
* Thanks to flypigahoy for his ispiring blog post about copying settings and files over a new device.
* Thanks to jgibbon for the icon and the cover graphics.
* Apps backup by topias.
* Thanks to all the testers for being brave and patient.

## AI Disclosure
Talteen is based on a previous project of mine [My Data Transfer](https://github.com/fravaccaro/harbour-mydatatransfer), completely authored, designed and developed with no AI input.

Gemini Pro was used in Talteen to:

* Reimplement the logic in C++.
* Implement the Network transfer.

