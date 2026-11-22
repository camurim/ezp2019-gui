#!/usr/bin/env bash
set -euo pipefail

PREFIX="/usr/local"
BUILD_DIR="build"
BUILD_GUI="ON"
INSTALL_UDEV="ON"
DO_UNINSTALL="OFF"

usage() {
    cat <<EOF
Uso: ./install.sh [opções]

Opções:
  --prefix <dir>   Prefixo de instalação (padrão: /usr/local)
  --no-gui         Compila e instala apenas a biblioteca e a CLI (sem Qt 6 GUI)
  --no-udev        Não instala a regra udev (/etc/udev/rules.d/99-ezp2019.rules)
  --uninstall      Remove os arquivos instalados pelo script
  -h, --help       Exibe esta ajuda
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)
            PREFIX="$2"
            shift 2
            ;;
        --no-gui)
            BUILD_GUI="OFF"
            shift
            ;;
        --no-udev)
            INSTALL_UDEV="OFF"
            shift
            ;;
        --uninstall)
            DO_UNINSTALL="ON"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Opção desconhecida: $1" >&2
            usage
            exit 1
            ;;
    esac
done

SUDO=""
if [[ $EUID -ne 0 ]]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    elif command -v pkexec >/dev/null 2>&1; then
        SUDO="pkexec"
    fi
fi

if [[ "$DO_UNINSTALL" == "ON" ]]; then
    echo "==> Removendo instalação de $PREFIX..."
    if [[ -f "$BUILD_DIR/install_manifest.txt" ]]; then
        while IFS= read -r file; do
            [[ -n "$file" ]] && $SUDO rm -f "$file"
        done < "$BUILD_DIR/install_manifest.txt"
    else
        $SUDO rm -f \
            "$PREFIX/bin/ezp2019" \
            "$PREFIX/bin/ezp2019-gui" \
            "$PREFIX/include/ezp2019.h" \
            "$PREFIX/lib/libezp2019.so"* \
            "$PREFIX/lib/pkgconfig/ezp2019.pc" \
            "$PREFIX/share/applications/ezp2019-gui.desktop" \
            "$PREFIX/share/icons/hicolor/scalable/apps/ezp2019-gui.svg"
    fi
    if [[ "$INSTALL_UDEV" == "ON" && -f /etc/udev/rules.d/99-ezp2019.rules ]]; then
        $SUDO rm -f /etc/udev/rules.d/99-ezp2019.rules
        $SUDO udevadm control --reload-rules || true
    fi
    $SUDO ldconfig 2>/dev/null || true
    echo "==> Desinstalação concluída."
    exit 0
fi

# Verificar dependências básicas
missing=()
for cmd in cmake pkg-config python3; do
    command -v "$cmd" >/dev/null 2>&1 || missing+=("$cmd")
done
pkg-config --exists libusb-1.0 2>/dev/null || missing+=("libusb-1.0-dev")

if [[ "$BUILD_GUI" == "ON" ]]; then
    command -v g++ >/dev/null 2>&1 || command -v clang++ >/dev/null 2>&1 || missing+=("g++")
    pkg-config --exists Qt6Widgets 2>/dev/null || missing+=("Qt6Widgets")
    pkg-config --exists Qt6Svg 2>/dev/null || missing+=("Qt6Svg")
fi

if [[ ${#missing[@]} -gt 0 ]]; then
    echo "ERRO: Dependências ausentes: ${missing[*]}" >&2
    echo "" >&2
    echo "Instale as dependências conforme sua distribuição:" >&2
    echo "  Ubuntu/Debian: sudo apt install cmake gcc g++ pkg-config python3 libusb-1.0-0-dev qt6-base-dev qt6-svg-dev" >&2
    echo "  Fedora:        sudo dnf install cmake gcc gcc-c++ pkgconf-pkg-config python3 libusb1-devel qt6-qtbase-devel qt6-qtsvg-devel" >&2
    echo "  Arch Linux:    sudo pacman -S cmake gcc pkgconf python libusb qt6-base qt6-svg" >&2
    exit 1
fi

echo "==> Configurando build (Release, PREFIX=$PREFIX, BUILD_GUI=$BUILD_GUI)..."
cmake -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_GUI="$BUILD_GUI"

echo "==> Compilando..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "==> Instalando em $PREFIX..."
if [[ -w "$PREFIX" ]]; then
    cmake --install "$BUILD_DIR"
else
    $SUDO cmake --install "$BUILD_DIR"
    $SUDO ldconfig 2>/dev/null || true
fi

if [[ "$INSTALL_UDEV" == "ON" ]]; then
    echo "==> Instalando regra udev (/etc/udev/rules.d/99-ezp2019.rules)..."
    $SUDO install -Dm644 99-ezp2019.rules /etc/udev/rules.d/99-ezp2019.rules
    $SUDO udevadm control --reload-rules
    $SUDO udevadm trigger --attr-match=idVendor=1fc8 --attr-match=idProduct=310b || true
fi

if [[ "$BUILD_GUI" == "ON" ]]; then
    command -v update-desktop-database >/dev/null 2>&1 && \
        $SUDO update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && \
        $SUDO gtk-update-icon-cache -f -t "$PREFIX/share/icons/hicolor" 2>/dev/null || true
fi

echo ""
echo "==> Instalação concluída com sucesso!"
echo "    CLI: $PREFIX/bin/ezp2019"
if [[ "$BUILD_GUI" == "ON" ]]; then
    echo "    GUI: $PREFIX/bin/ezp2019-gui (também disponível no menu de aplicativos)"
fi
echo "    Se o EZP2019 já estava conectado, desconecte e reconecte o cabo USB caso necessário."
