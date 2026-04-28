#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  scripts/decrypt-v2.sh -i backup.talteen -o output_dir [-p password]

Options:
  -i  Path to .talteen backup (required)
  -o  Output directory for extracted files (required)
  -p  Password (optional; if omitted, prompt securely)
  -w  Work directory (optional; default: mktemp)
  -k  Keep work directory (optional)
  -h  Show help
EOF
}

BACKUP=""
OUTDIR=""
PASSWORD=""
WORKDIR=""
KEEP_WORK=0

while getopts ":i:o:p:w:kh" opt; do
  case "$opt" in
    i) BACKUP="$OPTARG" ;;
    o) OUTDIR="$OPTARG" ;;
    p) PASSWORD="$OPTARG" ;;
    w) WORKDIR="$OPTARG" ;;
    k) KEEP_WORK=1 ;;
    h) usage; exit 0 ;;
    \?) echo "Unknown option: -$OPTARG" >&2; usage; exit 1 ;;
    :) echo "Missing argument for -$OPTARG" >&2; usage; exit 1 ;;
  esac
done

if [[ -z "$BACKUP" || -z "$OUTDIR" ]]; then
  usage
  exit 1
fi

if [[ ! -f "$BACKUP" ]]; then
  echo "Backup file not found: $BACKUP" >&2
  exit 1
fi

if [[ -z "$PASSWORD" ]]; then
  read -r -s -p "Backup password: " PASSWORD
  echo
fi

if [[ -z "$WORKDIR" ]]; then
  WORKDIR="$(mktemp -d -t talteen-v2-XXXXXX)"
fi
mkdir -p "$WORKDIR"
mkdir -p "$OUTDIR"

cleanup() {
  if [[ "$KEEP_WORK" -eq 0 ]]; then
    rm -rf "$WORKDIR"
  else
    echo "Keeping workdir: $WORKDIR"
  fi
}
trap cleanup EXIT

MANIFEST="$WORKDIR/manifest.yaml"
PAYLOAD="$WORKDIR/payload.enc"
TARXZ="$WORKDIR/payload.tar.xz"

echo "[1/5] Extracting outer backup..."
tar -xf "$BACKUP" -C "$WORKDIR"

if [[ ! -f "$MANIFEST" || ! -f "$PAYLOAD" ]]; then
  echo "Invalid backup: manifest.yaml or payload.enc missing" >&2
  exit 1
fi

get_manifest_value() {
  local key="$1"
  awk -F': ' -v k="$key" '$1 == k {gsub(/"/,"",$2); print $2; exit}' "$MANIFEST"
}

VERSION="$(get_manifest_value version)"
ENC="$(get_manifest_value encryption)"
KDF="$(get_manifest_value kdf)"
ITER="$(get_manifest_value kdf_iterations)"
SALT_B64="$(get_manifest_value salt_b64)"
IV_B64="$(get_manifest_value iv_b64)"
TAG_B64="$(get_manifest_value tag_b64)"
AAD="$(get_manifest_value aad)"
ENCRYPTED="$(get_manifest_value encrypted)"

if [[ "$VERSION" != "2.0.0" ]]; then
  echo "Unsupported version: $VERSION (expected 2.0.0)" >&2
  exit 1
fi
if [[ "$ENC" != "openssl-aes-256-gcm" ]]; then
  echo "Unsupported encryption: $ENC" >&2
  exit 1
fi
if [[ "$KDF" != "pbkdf2-hmac-sha256" ]]; then
  echo "Unsupported KDF: $KDF" >&2
  exit 1
fi
if [[ "$ENCRYPTED" != "true" ]]; then
  echo "Invalid manifest: encrypted must be true" >&2
  exit 1
fi
if [[ -z "$ITER" || -z "$SALT_B64" || -z "$IV_B64" || -z "$TAG_B64" || -z "$AAD" ]]; then
  echo "Invalid manifest: missing v2 fields" >&2
  exit 1
fi
if [[ "$AAD" != "talteen:v2" ]]; then
  echo "Invalid AAD: $AAD" >&2
  exit 1
fi

echo "[2/5] Compiling temporary EVP decrypt helper..."

HELPER_SRC="$WORKDIR/decrypt_v2.cpp"
HELPER_BIN="$WORKDIR/decrypt_v2"

cat > "$HELPER_SRC" <<'CPP'
#include <openssl/bio.h>
#include <openssl/evp.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

static std::vector<unsigned char> b64decode(const std::string &in)
{
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO *bio = BIO_new_mem_buf(in.data(), (int)in.size());
    bio = BIO_push(b64, bio);
    std::vector<unsigned char> out(in.size());
    int n = BIO_read(bio, out.data(), (int)out.size());
    BIO_free_all(bio);
    if (n < 0)
        return {};
    out.resize(n);
    return out;
}

int main(int argc, char **argv)
{
    if (argc != 9)
    {
        std::cerr << "Usage: decrypt_v2 <in.enc> <out.tar.xz> <password> <iter> <salt_b64> <iv_b64> <tag_b64> <aad>\n";
        return 2;
    }

    const char *inPath = argv[1];
    const char *outPath = argv[2];
    std::string password = argv[3];
    int iter = std::stoi(argv[4]);
    auto salt = b64decode(argv[5]);
    auto iv = b64decode(argv[6]);
    auto tag = b64decode(argv[7]);
    std::string aad = argv[8];

    if (salt.size() != 16 || iv.size() != 12 || tag.size() != 16 || iter <= 0)
    {
        std::cerr << "Invalid manifest crypto parameters\n";
        return 3;
    }

    std::vector<unsigned char> key(32);
    if (PKCS5_PBKDF2_HMAC(password.c_str(), (int)password.size(),
                          salt.data(), (int)salt.size(),
                          iter, EVP_sha256(),
                          (int)key.size(), key.data()) != 1)
    {
        std::cerr << "PBKDF2 failed\n";
        return 4;
    }

    std::ifstream in(inPath, std::ios::binary);
    if (!in)
    {
        std::cerr << "Cannot open input\n";
        return 5;
    }
    std::ofstream out(outPath, std::ios::binary);
    if (!out)
    {
        std::cerr << "Cannot open output\n";
        return 6;
    }

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return 7;

    int ok = 1;
    int len = 0;
    ok &= EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    ok &= EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)iv.size(), nullptr);
    ok &= EVP_DecryptInit_ex(ctx, nullptr, nullptr, key.data(), iv.data());
    ok &= EVP_DecryptUpdate(ctx, nullptr, &len, (const unsigned char *)aad.data(), (int)aad.size());

    if (!ok)
    {
        EVP_CIPHER_CTX_free(ctx);
        std::cerr << "Decrypt init failed\n";
        return 8;
    }

    std::vector<unsigned char> inbuf(65536), outbuf(65536 + 32);
    while (in)
    {
        in.read((char *)inbuf.data(), (std::streamsize)inbuf.size());
        std::streamsize got = in.gcount();
        if (got <= 0)
            break;
        if (EVP_DecryptUpdate(ctx, outbuf.data(), &len, inbuf.data(), (int)got) != 1)
        {
            EVP_CIPHER_CTX_free(ctx);
            std::cerr << "Decrypt update failed\n";
            return 9;
        }
        out.write((char *)outbuf.data(), len);
    }

    ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, (int)tag.size(), tag.data());
    if (!ok)
    {
        EVP_CIPHER_CTX_free(ctx);
        std::cerr << "Set tag failed\n";
        return 10;
    }

    int finalLen = 0;
    ok = EVP_DecryptFinal_ex(ctx, outbuf.data(), &finalLen);
    EVP_CIPHER_CTX_free(ctx);

    if (ok != 1)
    {
        std::cerr << "Auth failed (wrong password or modified backup)\n";
        return 11;
    }

    if (finalLen > 0)
        out.write((char *)outbuf.data(), finalLen);
    out.flush();
    return 0;
}
CPP

CXX="${CXX:-g++}"
"$CXX" -O2 -std=c++11 "$HELPER_SRC" -o "$HELPER_BIN" -lcrypto

echo "[3/5] Decrypting payload (AES-256-GCM)..."
"$HELPER_BIN" "$PAYLOAD" "$TARXZ" "$PASSWORD" "$ITER" "$SALT_B64" "$IV_B64" "$TAG_B64" "$AAD"

echo "[4/5] Extracting decrypted payload..."
tar -xJf "$TARXZ" -C "$OUTDIR"

echo "[5/5] Done."
echo "Restored files at: $OUTDIR"
