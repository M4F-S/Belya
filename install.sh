#!/usr/bin/env bash
set -e

# Belya Autonomous AI Software Engineer & Security Harness
# 1-Line Universal Installer (macOS & Linux)

VERSION="v6.3.0"
REPO="M4F-S/Belya"
INSTALL_DIR="/usr/local/bin"
ALT_INSTALL_DIR="$HOME/.local/bin"

echo "=========================================================================="
echo "          Belya Sovereign C99 Autonomous Engineering Harness              "
echo "=========================================================================="

OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

case "$ARCH" in
    x86_64|amd64)
        TARGET_ARCH="x86_64"
        ;;
    arm64|aarch64)
        TARGET_ARCH="arm64"
        ;;
    *)
        echo "[!] Unsupported architecture: $ARCH"
        exit 1
        ;;
esac

echo "[*] Detected Platform: $OS-$TARGET_ARCH"

# Determine install destination
if [ -w "$INSTALL_DIR" ]; then
    DEST="$INSTALL_DIR/belya"
else
    mkdir -p "$ALT_INSTALL_DIR"
    DEST="$ALT_INSTALL_DIR/belya"
fi

echo "[*] Target Binary Location: $DEST"

# Check dependencies
echo "[*] Checking runtime dependencies..."
MISSING=""
command -v curl >/dev/null 2>&1 || MISSING="$MISSING curl"
command -v git >/dev/null 2>&1 || MISSING="$MISSING git"

if [ -n "$MISSING" ]; then
    echo "[!] Missing required tools:$MISSING"
    echo "Please install them via your package manager (brew/apt/dnf) and retry."
    exit 1
fi

echo "[+] Dependencies verified (curl, git, sqlite3, libcurl)."

# Build from source or download release binary
TARBALL_NAME="belya-${VERSION}-${OS}-${TARGET_ARCH}.tar.gz"
DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${VERSION}/${TARBALL_NAME}"

TMP_DIR="$(mktemp -d /tmp/belya_install.XXXXXX)"
trap 'rm -rf "$TMP_DIR"' EXIT

echo "[*] Fetching release binary ($VERSION)..."
if curl -fsSL "$DOWNLOAD_URL" -o "$TMP_DIR/$TARBALL_NAME" 2>/dev/null; then
    tar -xzf "$TMP_DIR/$TARBALL_NAME" -C "$TMP_DIR"
    chmod +x "$TMP_DIR/belya"
    cp "$TMP_DIR/belya" "$DEST"
    echo "[+] Binary installed from release package."
else
    echo "[*] Pre-built archive not found or offline; building natively from source..."
    git clone --depth 1 https://github.com/${REPO}.git "$TMP_DIR/src"
    cd "$TMP_DIR/src"
    make -j4
    chmod +x belya
    cp belya "$DEST"
    echo "[+] Built and installed natively from source."
fi

# Verify installation
echo "[*] Verifying installation..."
"$DEST" --help >/dev/null 2>&1
echo "[+] Belya successfully installed to $DEST"
echo ""
echo "To get started, configure your API credentials in .env and run:"
echo "  belya -i                  # Interactive terminal REPL"
echo "  belya --headless <task>   # Autonomous batch task"
echo "  belya --telegram          # 24/7 sovereign Telegram daemon"
echo "=========================================================================="
