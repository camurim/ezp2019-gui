#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

APP_ID="io.github.camurim.Ezp2019Gui"
RUNTIME_VERSION="6.11"
MANIFEST="flatpak/${APP_ID}.yml"
METAINFO="flatpak/${APP_ID}.metainfo.xml"
DESKTOP_FILE="ezp2019-gui.desktop"
BUILD_DIR="flatpak-build"
REPO_DIR="flatpak-repo"
BUNDLE_FILE="ezp2019-gui.flatpak"

DO_INSTALL="OFF"
DO_INSTALL_DEPS="OFF"
DO_LINT="OFF"
DO_CLEAN="OFF"
SKIP_BUNDLE="OFF"
USE_FLATHUB_MANIFEST="OFF"

usage() {
    cat <<EOF
Uso: ./build-flatpak.sh [opções]

Empacota o EZP2019 USB Programmer (${APP_ID}) no formato Flatpak.

Opções:
  --install           Instala o pacote compilado localmente (--user) após o build
  --install-deps      Instala automaticamente Runtime/SDK (org.kde.Platform/Sdk ${RUNTIME_VERSION})
                      e org.flatpak.Builder (se flatpak-builder nativo não existir)
  --flathub-manifest  Usa o manifesto de submissão ao Flathub (fonte git remota)
  --lint              Executa o flatpak-builder-lint (requer org.flatpak.Builder)
  --no-bundle         Não gera o arquivo standalone ${BUNDLE_FILE}
  --clean             Remove diretórios temporários de build (.flatpak-builder, ${BUILD_DIR}, ${REPO_DIR})
  -h, --help          Exibe esta ajuda

Exemplos:
  ./build-flatpak.sh --install-deps --install   # Instala SDK, compila, gera .flatpak e instala localmente
  ./build-flatpak.sh                            # Compila e gera ${BUNDLE_FILE}
  ./build-flatpak.sh --lint                     # Compila e valida conformidade com o Flathub
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --install)
            DO_INSTALL="ON"
            shift
            ;;
        --install-deps)
            DO_INSTALL_DEPS="ON"
            shift
            ;;
        --flathub-manifest)
            USE_FLATHUB_MANIFEST="ON"
            MANIFEST="flatpak/${APP_ID}.flathub.yml"
            shift
            ;;
        --lint)
            DO_LINT="ON"
            shift
            ;;
        --no-bundle)
            SKIP_BUNDLE="ON"
            shift
            ;;
        --clean)
            DO_CLEAN="ON"
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "ERRO: Opção desconhecida: $1" >&2
            usage
            exit 1
            ;;
    esac
done

if [[ "$DO_CLEAN" == "ON" ]]; then
    echo "==> Limpando diretórios temporários de build Flatpak..."
    rm -rf .flatpak-builder "$BUILD_DIR" "$REPO_DIR"
    echo "==> Limpeza concluída."
    exit 0
fi

# 1. Verificar flatpak
if ! command -v flatpak >/dev/null 2>&1; then
    echo "ERRO: 'flatpak' não está instalado no sistema." >&2
    echo "Instale via gerenciador de pacotes (ex: sudo apt install flatpak flatpak-builder)." >&2
    exit 1
fi

# 2. Determinar comando do flatpak-builder (nativo ou via org.flatpak.Builder)
FLATPAK_BUILDER_CMD=()
if command -v flatpak-builder >/dev/null 2>&1; then
    FLATPAK_BUILDER_CMD=(flatpak-builder)
elif flatpak info org.flatpak.Builder >/dev/null 2>&1; then
    FLATPAK_BUILDER_CMD=(flatpak run org.flatpak.Builder)
else
    if [[ "$DO_INSTALL_DEPS" == "ON" ]]; then
        echo "==> Instalando org.flatpak.Builder via Flathub..."
        flatpak remote-add --if-not-exists --user flathub https://dl.flathub.org/repo/flathub.flatpakrepo
        flatpak install -y --user flathub org.flatpak.Builder
        FLATPAK_BUILDER_CMD=(flatpak run org.flatpak.Builder)
    else
        echo "ERRO: 'flatpak-builder' não encontrado (nem nativo nem org.flatpak.Builder)." >&2
        echo "Instale com:" >&2
        echo "  flatpak install flathub org.flatpak.Builder" >&2
        echo "ou execute novamente com --install-deps." >&2
        exit 1
    fi
fi

# 3. Verificar Runtime e SDK
missing_refs=()
for ref in "org.kde.Platform//${RUNTIME_VERSION}" "org.kde.Sdk//${RUNTIME_VERSION}"; do
    if ! flatpak info "$ref" >/dev/null 2>&1; then
        missing_refs+=("$ref")
    fi
done

if [[ ${#missing_refs[@]} -gt 0 ]]; then
    if [[ "$DO_INSTALL_DEPS" == "ON" ]]; then
        echo "==> Instalando Runtime/SDK ausentes: ${missing_refs[*]}..."
        flatpak remote-add --if-not-exists --user flathub https://dl.flathub.org/repo/flathub.flatpakrepo
        flatpak install -y --user flathub "${missing_refs[@]}"
    else
        echo "ERRO: Runtime/SDK Flatpak ausentes: ${missing_refs[*]}" >&2
        echo "Instale com:" >&2
        echo "  flatpak install flathub ${missing_refs[*]}" >&2
        echo "ou execute novamente com --install-deps." >&2
        exit 1
    fi
fi

# 4. Validar metadados antes do build
if command -v desktop-file-validate >/dev/null 2>&1; then
    echo "==> Validando ${DESKTOP_FILE}..."
    desktop-file-validate "$DESKTOP_FILE"
fi

if command -v appstreamcli >/dev/null 2>&1; then
    echo "==> Validando ${METAINFO}..."
    appstreamcli validate --no-net "$METAINFO"
fi

# 5. Compilar e exportar para repositório OSTree local
echo "==> Compilando Flatpak usando ${MANIFEST}..."
BUILD_ARGS=(
    --force-clean
    --user
    --repo="$REPO_DIR"
)

if [[ "$DO_INSTALL" == "ON" ]]; then
    BUILD_ARGS+=(--install)
fi

"${FLATPAK_BUILDER_CMD[@]}" "${BUILD_ARGS[@]}" "$BUILD_DIR" "$MANIFEST"

# 6. Gerar bundle standalone (.flatpak)
if [[ "$SKIP_BUNDLE" == "OFF" ]]; then
    echo "==> Gerando bundle standalone: ${BUNDLE_FILE}..."
    flatpak build-bundle "$REPO_DIR" "$BUNDLE_FILE" "$APP_ID" \
        --runtime-repo=https://flathub.org/repo/flathub.flatpakrepo
fi

# 7. Validação Flathub (flatpak-builder-lint), se solicitada
if [[ "$DO_LINT" == "ON" ]]; then
    if flatpak info org.flatpak.Builder >/dev/null 2>&1; then
        echo "==> Executando flatpak-builder-lint (manifesto e repositório)..."
        flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest "$MANIFEST" || true
        flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo "$REPO_DIR" || true
    else
        echo "AVISO: --lint requer org.flatpak.Builder instalado:" >&2
        echo "  flatpak install flathub org.flatpak.Builder" >&2
    fi
fi

echo ""
echo "==> Empacotamento Flatpak concluído com sucesso!"
if [[ "$SKIP_BUNDLE" == "OFF" ]]; then
    echo "    Bundle gerado: ${SCRIPT_DIR}/${BUNDLE_FILE}"
    echo "    Para instalar o bundle em qualquer máquina Linux:"
    echo "      flatpak install --user ./${BUNDLE_FILE}"
fi
if [[ "$DO_INSTALL" == "ON" ]]; then
    echo "    Aplicação instalada localmente. Execute com:"
    echo "      flatpak run ${APP_ID}"
fi

