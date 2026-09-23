#!/usr/bin/env bash
set -uo pipefail

ACC=$'\033[91m'; GRN=$'\033[32m'; YEL=$'\033[33m'
BLD=$'\033[1m'; DIM=$'\033[2m'; RST=$'\033[0m'
MASCOT="/etc/snapos/mascot-console.txt"
[ -r "$MASCOT" ] || MASCOT="/etc/snapos/mascot.txt"
BUILD_ID="$(cat /etc/snapos-build 2>/dev/null || echo "?")"
TOTAL=10
STEP=0
STEPNAME=""
L=en

declare -A EN PT

EN[title]="Installer"
PT[title]="Instalador"
EN[step]="Step"
PT[step]="Etapa"
EN[s_net]="Network"
PT[s_net]="Rede"
EN[s_kb]="Keyboard"
PT[s_kb]="Teclado"
EN[s_loc]="Language and region"
PT[s_loc]="Idioma e regiao"
EN[s_tz]="Time zone"
PT[s_tz]="Fuso horario"
EN[s_disk]="Disk"
PT[s_disk]="Disco"
EN[s_acct]="Account"
PT[s_acct]="Conta"
EN[s_gfx]="Graphics"
PT[s_gfx]="Graficos"
EN[gfx_title]="Graphics mode"
PT[gfx_title]="Modo grafico"
EN[gfx_detected]="Detected graphics:"
PT[gfx_detected]="Graficos detectados:"
EN[gfx_hybrid]="This PC has more than one graphics chip."
PT[gfx_hybrid]="Este PC tem mais de um chip grafico."
EN[gfx_auto]="Standard (recommended for most PCs)"
PT[gfx_auto]="Padrao (recomendado para a maioria dos PCs)"
EN[gfx_intel]="Intel graphics only (best for laptops with two graphics chips)"
PT[gfx_intel]="Somente graficos Intel (melhor para notebooks com dois chips de video)"
EN[r_gfx]="Graphics"
PT[r_gfx]="Graficos"
EN[intro_title]="Install in ten steps"
PT[intro_title]="Instale em dez passos"
EN[intro_done]="Ready to install"
PT[intro_done]="Pronto para instalar"
EN[s_look]="Appearance"
PT[s_look]="Aparencia"
EN[look_title]="Choose the look of the system"
PT[look_title]="Escolha a aparencia do sistema"
EN[look_dark]="Dark (recommended)"
PT[look_dark]="Escuro (recomendado)"
EN[look_light]="Light"
PT[look_light]="Claro"
EN[r_look]="Appearance"
PT[r_look]="Aparencia"
EN[s_review]="Review"
PT[s_review]="Revisao"
EN[s_install]="Installing"
PT[s_install]="Instalando"
EN[welcome]="This sets up SnapOS on this computer. A few questions, then the installer downloads and configures everything."
PT[welcome]="Isto instala o SnapOS neste computador. Algumas perguntas, depois o instalador baixa e configura tudo."
EN[welcome_hint]="Press Enter to start. Press Ctrl+C to open a shell."
PT[welcome_hint]="Enter para comecar. Ctrl+C para abrir um terminal."
EN[inst_title]="SnapOS is already installed on this computer."
PT[inst_title]="O SnapOS ja esta instalado neste computador."
EN[inst_hint]="Restart, and remove the USB stick when the screen goes dark."
PT[inst_hint]="Reinicie e remova o pen drive quando a tela apagar."
EN[inst_reboot]="Restart now"
PT[inst_reboot]="Reiniciar agora"
EN[inst_update]="Update SnapOS (keeps your files)"
PT[inst_update]="Atualizar o SnapOS (mantem seus arquivos)"
EN[u_bak]="Your previous configuration.nix was saved as configuration.nix.bak."
PT[u_bak]="Seu configuration.nix anterior foi salvo como configuration.nix.bak."
EN[inst_again]="Install again (erases the disk)"
PT[inst_again]="Instalar de novo (apaga o disco)"
EN[inst_shell]="Open a shell"
PT[inst_shell]="Abrir um terminal"
EN[net_ok]="Connected to the internet."
PT[net_ok]="Conectado a internet."
EN[net_search]="No connection yet. Looking for Wi-Fi networks..."
PT[net_search]="Sem conexao ainda. Procurando redes Wi-Fi..."
EN[net_nonm]="NetworkManager is not available on this image. Plug in a network cable."
PT[net_nonm]="NetworkManager indisponivel nesta imagem. Conecte um cabo de rede."
EN[net_retry]="Press Enter to try again."
PT[net_retry]="Enter para tentar de novo."
EN[net_none]="No Wi-Fi networks found."
PT[net_none]="Nenhuma rede Wi-Fi encontrada."
EN[net_report]="Wi-Fi hardware report (take a photo of this if Wi-Fi never shows up):"
PT[net_report]="Relatorio do hardware Wi-Fi (tire uma foto disto se o Wi-Fi nunca aparecer):"
EN[net_cable]="Press Enter to search again, or type 'cable' if you plugged one in: "
PT[net_cable]="Enter para procurar de novo, ou digite 'cabo' se conectou um cabo: "
EN[net_list]="Wi-Fi networks"
PT[net_list]="Redes Wi-Fi"
EN[net_rescan]="Search again"
PT[net_rescan]="Procurar de novo"
EN[net_pass]="Password for @1 (leave empty for an open network)"
PT[net_pass]="Senha de @1 (deixe vazio se a rede for aberta)"
EN[net_connecting]="Connecting..."
PT[net_connecting]="Conectando..."
EN[net_conn]="Connected to @1."
PT[net_conn]="Conectado a @1."
EN[net_fail]="Could not connect. Try again."
PT[net_fail]="Nao foi possivel conectar. Tente de novo."
EN[p_search]="Search the full list"
PT[p_search]="Buscar na lista completa"
EN[p_type]="Type a value"
PT[p_type]="Digitar um valor"
EN[p_term]="Part of the name (for example: sao_paulo, br, pt_BR): "
PT[p_term]="Parte do nome (ex: sao_paulo, br, pt_BR): "
EN[p_none]="Nothing found."
PT[p_none]="Nada encontrado."
EN[p_value]="Value: "
PT[p_value]="Valor: "
EN[kb_title]="Keyboard layout"
PT[kb_title]="Layout do teclado"
EN[loc_title]="Language and region"
PT[loc_title]="Idioma e regiao"
EN[tz_title]="Time zone"
PT[tz_title]="Fuso horario"
EN[disk_title]="Choose the disk to install on"
PT[disk_title]="Escolha o disco para instalar"
EN[disk_prompt]="Disk (for example sda or nvme0n1): "
PT[disk_prompt]="Disco (ex: sda ou nvme0n1): "
EN[disk_missing]="Disk not found: @1"
PT[disk_missing]="Disco nao encontrado: @1"
EN[user]="User name"
PT[user]="Nome de usuario"
EN[pass]="Password for @1"
PT[pass]="Senha de @1"
EN[confirm]="Confirm"
PT[confirm]="Confirme"
EN[pass_empty]="The password cannot be empty."
PT[pass_empty]="A senha nao pode ficar vazia."
EN[pass_diff]="The passwords do not match."
PT[pass_diff]="As senhas nao conferem."
EN[host]="Computer name"
PT[host]="Nome do computador"
EN[r_kb]="Keyboard"
PT[r_kb]="Teclado"
EN[r_loc]="Language"
PT[r_loc]="Idioma"
EN[r_tz]="Time zone"
PT[r_tz]="Fuso horario"
EN[r_disk]="Disk"
PT[r_disk]="Disco"
EN[r_user]="User"
PT[r_user]="Usuario"
EN[r_host]="Computer"
PT[r_host]="Computador"
EN[erase]="ALL DATA ON THIS DISK WILL BE ERASED"
PT[erase]="TODOS OS DADOS DESTE DISCO SERAO APAGADOS"
EN[save]="Type SAVE to install (anything else cancels): "
PT[save]="Digite SAVE para instalar (qualquer outra coisa cancela): "
EN[cancel]="Cancelled. Nothing was changed."
PT[cancel]="Cancelado. Nada foi alterado."
EN[i_part]="Partitioning @1"
PT[i_part]="Particionando @1"
EN[i_fmt]="Formatting"
PT[i_fmt]="Formatando"
EN[i_mount]="Mounting"
PT[i_mount]="Montando"
EN[i_hw]="Detecting this hardware"
PT[i_hw]="Detectando este hardware"
EN[i_copy]="Copying SnapOS"
PT[i_copy]="Copiando o SnapOS"
EN[i_run]="Installing SnapOS. This downloads several GB and can take a while."
PT[i_run]="Instalando o SnapOS. Baixa varios GB e pode demorar."
EN[i_pw]="Setting the password for @1"
PT[i_pw]="Definindo a senha de @1"
EN[i_pwfail]="Could not set the password. After booting, run: passwd"
PT[i_pwfail]="Nao foi possivel definir a senha. Apos o boot, rode: passwd"
EN[f_part]="Partitioning failed."
PT[f_part]="Falha ao particionar."
EN[f_fmt]="Formatting failed: @1"
PT[f_fmt]="Falha ao formatar: @1"
EN[f_mount]="Mounting failed: @1"
PT[f_mount]="Falha ao montar: @1"
EN[f_hw]="nixos-generate-config failed."
PT[f_hw]="nixos-generate-config falhou."
EN[f_install]="nixos-install failed. The disk was not finalized. See the error above."
PT[f_install]="nixos-install falhou. O disco nao foi finalizado. Veja o erro acima."
EN[rerun]="Run the installer again with: sudo /etc/snapos-installer/snap-install-nixos.sh"
PT[rerun]="Rode o instalador de novo com: sudo /etc/snapos-installer/snap-install-nixos.sh"
EN[done_title]="SnapOS is installed."
PT[done_title]="O SnapOS foi instalado."
EN[done_hint]="Press Enter to restart. Remove the USB stick when the screen goes dark."
PT[done_hint]="Pressione Enter para reiniciar. Remova o pen drive quando a tela apagar."

t() {
    local k="$1" v i=1 a
    shift
    if [ "$L" = pt ]; then v="${PT[$k]-}"; else v="${EN[$k]-}"; fi
    [ -n "$v" ] || v="${EN[$k]-$k}"
    for a in "$@"; do v="${v//@$i/"$a"}"; i=$((i+1)); done
    printf '%s' "$v"
}

header() {
    clear
    printf '\n  %s%sSnapOS%s  %s%s   build %s%s\n' "$BLD" "$ACC" "$RST" "$DIM" "$(t title)" "$BUILD_ID" "$RST"
    if [ "$STEP" -gt 0 ]; then
        printf '  %s%s %s/%s  ·  %s%s\n' "$DIM" "$(t step)" "$STEP" "$TOTAL" "$STEPNAME" "$RST"
    fi
    printf '  %s──────────────────────────────────────────────%s\n\n' "$DIM" "$RST"
}

step() { STEP=$1; STEPNAME="$(t "$2")"; }

# The installer opens with Snappy walking through the ten steps. Any key skips it.
INTRO_SKIP=0
intro_pause() {
    [ "$INTRO_SKIP" = 1 ] && return 0
    if read -rsn1 -t "$1" _key 2>/dev/null; then INTRO_SKIP=1; fi
    return 0
}
intro_at() { printf '\033[%d;%dH%s' "$1" "${3:-42}" "$2"; }
intro_track() {
    # nodes before $1 are done, node $1 is the current step, the rest are waiting
    local i j="" total=10
    for ((i = 0; i < total; i++)); do
        if [ "$i" -lt "$1" ]; then j+="${ACC}▓${RST}"
        elif [ "$i" -eq "$1" ]; then j+="${BLD}${ACC}◉${RST}"
        else j+="${DIM}▒${RST}"; fi
        [ "$i" -lt $((total - 1)) ] && j+="${DIM}──${RST}"
    done
    intro_at 8 "$j"
}
intro_text() {
    [ -t 1 ] || return 0
    local keys=(s_net s_kb s_loc s_tz s_disk s_acct s_look s_gfx s_review s_install) i
    intro_at 5 "${BLD}${ACC}# $(t intro_title)${RST}"
    intro_pause 0.4
    for i in "${!keys[@]}"; do
        intro_track "$i"
        intro_at 10 "${DIM}$(t step) $((i + 1))/10${RST}      "
        intro_at 11 "${BLD}$(printf '%-26s' "$(t "${keys[$i]}")")${RST}"
        intro_pause 0.28
    done
    intro_track 10
    intro_at 10 "$(printf '%-26s' '')"
    intro_at 11 "${GRN}✓${RST} ${BLD}$(printf '%-24s' "$(t intro_done)")${RST}"
    # Snappy is happy
    intro_at 9 "${ACC}    █    ┌┐  ^      ^  ┌┐    █${RST}" 3
    intro_at 10 "${ACC}     \\       \\ \\▼▼▼▼/ /       /${RST}" 3
    intro_pause 0.6
    printf '\033[14;1H'
}
say() { printf '  %s\n' "$*"; }
opt() { printf '    %s%s%s  %s\n' "$BLD" "$1" "$RST" "$2"; }
ok() { printf '  %s✓%s %s\n' "$GRN" "$RST" "$*"; }
warn() { printf '  %s%s%s\n' "$YEL" "$*" "$RST"; }
caret() { printf '\n  %s›%s ' "$ACC" "$RST"; }

die() {
    printf '\n  %s%s%s\n  %s%s%s\n\n' "$ACC" "$*" "$RST" "$DIM" "$(t rerun)" "$RST"
    exit 1
}

restart() {
    printf '\n'
    say "$(t done_hint)"
    read -r _
    sync
    systemctl reboot 2>/dev/null || reboot -f
    exit 0
}

choose_appearance() {
    local choice
    header
    say "${BLD}$(t look_title)${RST}"
    printf '\n'
    opt 1 "$(t look_dark)"
    opt 2 "$(t look_light)"
    caret
    read -r choice
    case "$choice" in
        2) LOOK=light; LOOKLABEL="$(t look_light)" ;;
        *) LOOK=dark;  LOOKLABEL="$(t look_dark)" ;;
    esac
}

choose_graphics() {
    local choice n
    header
    say "${BLD}$(t gfx_title)${RST}"
    printf '\n'
    GPUS="$(lspci 2>/dev/null | grep -iE 'vga|3d|display' | sed 's/^[^:]*: //')"
    GFXDEFAULT=1
    n="$(printf '%s\n' "$GPUS" | grep -c .)"
    if [ -n "$GPUS" ]; then
        say "$(t gfx_detected)"
        printf '%s\n' "$GPUS" | sed 's/^/    /'
        printf '\n'
        if [ "$n" -gt 1 ]; then
            warn "$(t gfx_hybrid)"
            printf '\n'
            if printf '%s\n' "$GPUS" | grep -qi intel; then GFXDEFAULT=2; fi
        fi
    fi
    opt 1 "$(t gfx_auto)"
    opt 2 "$(t gfx_intel)"
    caret
    read -r choice
    [ -n "$choice" ] || choice="$GFXDEFAULT"
    case "$choice" in
        2) GFXMODE=intel; GFXLABEL="$(t gfx_intel)" ;;
        *) GFXMODE=auto;  GFXLABEL="$(t gfx_auto)" ;;
    esac
}

# The network chosen here is kept, so the installed system connects to it by
# itself and updates can download right away.
keep_network() {
    local d=/mnt/etc/NetworkManager/system-connections f
    ls /etc/NetworkManager/system-connections/*.nmconnection >/dev/null 2>&1 || return 0
    mkdir -p "$d"
    for f in /etc/NetworkManager/system-connections/*.nmconnection; do
        install -m 600 -o root -g root "$f" "$d/$(basename "$f")"
    done
}

# /etc/nixos points at /etc/snapos, so NixOS tools find the system too. The
# release file records the build this system came from, for `snapos update`.
link_nixos() {
    rm -rf /mnt/etc/nixos
    ln -s snapos /mnt/etc/nixos
    cp /etc/snapos-build /mnt/etc/snapos/release 2>/dev/null || true
}

write_graphics() {
    case "$GFXMODE" in
        intel) printf '{ ... }: {\n  boot.blacklistedKernelModules = [ "nouveau" "nvidia" "radeon" "amdgpu" ];\n}\n' > /mnt/etc/snapos/graphics.nix ;;
        *)     printf '{ ... }: { }\n' > /mnt/etc/snapos/graphics.nix ;;
    esac
}

update_system() {
    local f
    step 10 s_install
    header
    say "$(t i_mount)..."
    mount /dev/disk/by-label/nixos /mnt || die "$(t f_mount nixos)"
    mkdir -p /mnt/boot
    mount /dev/disk/by-label/BOOT /mnt/boot || die "$(t f_mount BOOT)"
    say "$(t i_copy)..."
    STEP=0
    choose_graphics
    # older installs keep the system in /etc/nixos; it moves to /etc/snapos
    if [ -d /mnt/etc/nixos ] && [ ! -L /mnt/etc/nixos ] && [ ! -d /mnt/etc/snapos ]; then
        mv /mnt/etc/nixos /mnt/etc/snapos
    fi
    mkdir -p /mnt/etc/snapos /tmp/snapos-keep
    for f in hardware-configuration.nix local.nix; do
        [ -e "/mnt/etc/snapos/$f" ] && cp -a "/mnt/etc/snapos/$f" "/tmp/snapos-keep/$f"
    done
    [ -e /mnt/etc/snapos/configuration.nix ] && cp -a /mnt/etc/snapos/configuration.nix /mnt/etc/snapos/configuration.nix.bak
    cp -a /etc/snapos-src/. /mnt/etc/snapos/
    for f in hardware-configuration.nix local.nix; do
        [ -e "/tmp/snapos-keep/$f" ] && cp -a "/tmp/snapos-keep/$f" "/mnt/etc/snapos/$f"
    done
    write_graphics
    link_nixos
    keep_network
    say "$(t u_bak)"
    printf '\n  %s%s%s\n\n' "$BLD" "$(t i_run)" "$RST"
    LC_ALL=C.UTF-8 nixos-install --root /mnt --flake "path:/mnt/etc/snapos#snapos" --no-root-passwd \
        || die "$(t f_install)"
    printf '\n'
    ok "${BLD}$(t done_title)${RST}"
    restart
}

pick_from_list() {
    local title="$1" listcmd="$2" choice term pick i j
    shift 2
    local -a curated=("$@") matches
    while true; do
        header
        say "${BLD}$(t "$title")${RST}"
        printf '\n'
        i=1
        for c in "${curated[@]}"; do opt "$i" "$c"; i=$((i+1)); done
        opt "$i" "${DIM}$(t p_search)${RST}"
        opt "$((i+1))" "${DIM}$(t p_type)${RST}"
        caret
        read -r choice
        if [[ "$choice" =~ ^[0-9]+$ ]] && [ "$choice" -ge 1 ] && [ "$choice" -le "${#curated[@]}" ]; then
            ANSWER="${curated[$((choice-1))]}"; return 0
        elif [ "$choice" = "$i" ]; then
            printf '\n  %s' "$(t p_term)"
            read -r term
            mapfile -t matches < <(eval "$listcmd" 2>/dev/null | grep -i -- "$term" | head -30)
            if [ "${#matches[@]}" -eq 0 ]; then warn "$(t p_none)"; sleep 1; continue; fi
            printf '\n'
            j=1
            for m in "${matches[@]}"; do opt "$j" "$m"; j=$((j+1)); done
            caret
            read -r pick
            if [[ "$pick" =~ ^[0-9]+$ ]] && [ "$pick" -ge 1 ] && [ "$pick" -le "${#matches[@]}" ]; then
                ANSWER="${matches[$((pick-1))]}"; return 0
            fi
        elif [ "$choice" = "$((i+1))" ]; then
            printf '\n  %s' "$(t p_value)"
            read -r ANSWER
            [ -n "$ANSWER" ] && return 0
        fi
    done
}

ask() {
    printf '  %s%s%s' "$BLD" "$1" "$RST"
    caret
    read -r ANSWER
    [ -z "$ANSWER" ] && ANSWER="$2"
}

askpass() {
    local p1 p2
    while true; do
        printf '  %s%s%s' "$BLD" "$1" "$RST"
        caret
        read -rs p1
        printf '\n  %s%s%s' "$BLD" "$(t confirm)" "$RST"
        caret
        read -rs p2
        printf '\n'
        if [ -z "$p1" ]; then warn "$(t pass_empty)"; printf '\n'; continue; fi
        if [ "$p1" != "$p2" ]; then warn "$(t pass_diff)"; printf '\n'; continue; fi
        PASSWORD="$p1"; return 0
    done
}

is_online() { curl -fsS -m 5 -o /dev/null https://api.github.com 2>/dev/null; }

# When no network shows up, the screen says why: the card, its driver, radio
# blocks and firmware messages. A photo of this is enough to fix the image.
wifi_report() {
    printf '\n  %s%s%s\n' "$DIM" "$(t net_report)" "$RST"
    lspci -nnk 2>/dev/null | grep -A3 -iE 'network|wireless' | sed 's/^/    /'
    printf '    --\n'
    nmcli -t -f DEVICE,TYPE,STATE device 2>/dev/null | sed 's/^/    /'
    rfkill list 2>/dev/null | grep -iE 'wireless|blocked' | sed 's/^/    /'
    dmesg 2>/dev/null | grep -iE 'firmware|wlan|iwl|ath[0-9]|brcm|rtw|mt76' | tail -6 | cut -c1-90 | sed 's/^/    /'
    printf '\n'
}

network_connect() {
    local -a nets
    local r choice i n SSID WIFIPASS
    step 1 s_net
    header
    if is_online; then ok "$(t net_ok)"; sleep 1; return 0; fi
    warn "$(t net_search)"
    if ! command -v nmcli >/dev/null 2>&1; then
        warn "$(t net_nonm)"
        say "$(t net_retry)"; read -r _
        network_connect; return $?
    fi
    systemctl start NetworkManager >/dev/null 2>&1 || true
    rfkill unblock all >/dev/null 2>&1 || true
    nmcli radio wifi on >/dev/null 2>&1
    # right after boot the Wi-Fi card can still be loading its firmware
    for _wait in $(seq 1 20); do
        if nmcli -t -f TYPE,STATE device 2>/dev/null | grep -q '^wifi:' &&
           ! nmcli -t -f TYPE,STATE device 2>/dev/null | grep -q '^wifi:unavailable'; then break; fi
        sleep 1
    done
    sleep 2
    while true; do
        # a fresh driver can need more than one scan before it reports anything
        for _try in 1 2 3; do
            mapfile -t nets < <(nmcli -t -f SSID dev wifi list --rescan yes 2>/dev/null | awk -F: 'NF && $1!="" ' | sort -u)
            [ "${#nets[@]}" -gt 0 ] && break
            sleep 3
        done
        if [ "${#nets[@]}" -eq 0 ]; then
            warn "$(t net_none)"
            wifi_report
            printf '  %s' "$(t net_cable)"
            read -r r
            if [ "$r" = "cable" ] || [ "$r" = "cabo" ]; then
                if is_online; then ok "$(t net_ok)"; sleep 1; return 0; fi
            fi
            continue
        fi
        header
        say "${BLD}$(t net_list)${RST}"
        printf '\n'
        i=1
        for n in "${nets[@]}"; do opt "$i" "$n"; i=$((i+1)); done
        opt "$i" "${DIM}$(t net_rescan)${RST}"
        caret
        read -r choice
        if [ "$choice" = "$i" ]; then continue; fi
        if ! [[ "$choice" =~ ^[0-9]+$ ]] || [ "$choice" -lt 1 ] || [ "$choice" -gt "${#nets[@]}" ]; then continue; fi
        SSID="${nets[$((choice-1))]}"
        printf '\n  %s%s%s' "$BLD" "$(t net_pass "$SSID")" "$RST"
        caret
        read -rs WIFIPASS
        printf '\n  %s%s%s\n' "$DIM" "$(t net_connecting)" "$RST"
        if [ -n "$WIFIPASS" ]; then
            nmcli device wifi connect "$SSID" password "$WIFIPASS" >/dev/null 2>&1
        else
            nmcli device wifi connect "$SSID" >/dev/null 2>&1
        fi
        unset WIFIPASS
        sleep 2
        if is_online; then ok "$(t net_conn "$SSID")"; sleep 1; return 0; fi
        warn "$(t net_fail)"; sleep 2
    done
}

header
say "Language"
printf '\n'
opt 1 "English"
opt 2 "Portugues"
caret
read -r choice
[ "$choice" = 2 ] && L=pt

if [ -e /dev/disk/by-label/nixos ]; then
    header
    say "${BLD}$(t inst_title)${RST}"
    say "${DIM}$(t inst_hint)${RST}"
    printf '\n'
    opt 1 "$(t inst_reboot)"
    opt 2 "$(t inst_update)"
    opt 3 "$(t inst_again)"
    opt 4 "$(t inst_shell)"
    caret
    read -r choice
    case "$choice" in
        2) update_system ;;
        3) ;;
        4) exit 0 ;;
        *) restart ;;
    esac
fi

STEP=0
header
if [ -r "$MASCOT" ]; then
    while IFS= read -r l; do printf '  %s%s%s\n' "$ACC" "$l" "$RST"; done < "$MASCOT"
    printf '\n'
    intro_text
fi
say "$(t welcome)"
printf '\n'
say "${DIM}$(t welcome_hint)${RST}"
read -r _

network_connect

if [ "$L" = pt ]; then
    KB_LIST=("br-abnt2" "us" "us-intl" "pt-latin1" "es")
    LOC_LIST=("pt_BR.UTF-8" "en_US.UTF-8" "es_ES.UTF-8" "pt_PT.UTF-8")
    TZ_LIST=("America/Sao_Paulo" "America/Fortaleza" "America/Manaus" "UTC" "Europe/Lisbon")
else
    KB_LIST=("us" "us-intl" "uk" "br-abnt2" "de" "fr" "es")
    LOC_LIST=("en_US.UTF-8" "en_GB.UTF-8" "pt_BR.UTF-8" "es_ES.UTF-8" "de_DE.UTF-8" "fr_FR.UTF-8")
    TZ_LIST=("UTC" "America/New_York" "America/Sao_Paulo" "Europe/London" "Europe/Berlin")
fi

step 2 s_kb
pick_from_list kb_title "localectl list-keymaps" "${KB_LIST[@]}"
KEYMAP="$ANSWER"

step 3 s_loc
pick_from_list loc_title "localectl list-locales" "${LOC_LIST[@]}"
LOCALE="$ANSWER"

step 4 s_tz
pick_from_list tz_title "timedatectl list-timezones" "${TZ_LIST[@]}"
TIMEZONE="$ANSWER"

step 5 s_disk
header
say "${BLD}$(t disk_title)${RST}"
printf '\n'
lsblk -dno NAME,SIZE,MODEL -e 7,11 2>/dev/null | sed 's/^/    /'
printf '\n  %s' "$(t disk_prompt)"
read -r DISKNAME
TARGET="/dev/$DISKNAME"
[ -b "$TARGET" ] || die "$(t disk_missing "$TARGET")"

step 6 s_acct
header
ask "$(t user)" "snap"; USERNAME="$ANSWER"
printf '\n'
askpass "$(t pass "$USERNAME")"
printf '\n'
ask "$(t host)" "snapos"; HOSTNAME="$ANSWER"

step 7 s_look
choose_appearance

step 8 s_gfx
choose_graphics

step 9 s_review
header
printf '  %-12s %s\n' "$(t r_kb)" "$KEYMAP"
printf '  %-12s %s\n' "$(t r_loc)" "$LOCALE"
printf '  %-12s %s\n' "$(t r_tz)" "$TIMEZONE"
printf '  %-12s %s%s%s\n' "$(t r_disk)" "$BLD" "$TARGET" "$RST"
printf '  %-12s %s\n' "$(t r_user)" "$USERNAME"
printf '  %-12s %s\n' "$(t r_host)" "$HOSTNAME"
printf '  %-12s %s\n' "$(t r_look)" "$LOOKLABEL"
printf '  %-12s %s\n' "$(t r_gfx)" "$GFXLABEL"
printf '\n  %s%s%s\n\n' "$ACC" "$(t erase)" "$RST"
printf '  %s' "$(t save)"
read -r CONFIRM
[ "$CONFIRM" = "SAVE" ] || die "$(t cancel)"

step 10 s_install
header
say "$(t i_part "$TARGET")..."
parted -s "$TARGET" -- mklabel gpt \
    mkpart bios_boot 1MiB 2MiB set 1 bios_grub on \
    mkpart ESP fat32 2MiB 514MiB set 2 esp on \
    mkpart root ext4 514MiB 100% || die "$(t f_part)"
partprobe "$TARGET" 2>/dev/null; sleep 2

if [[ "$TARGET" =~ nvme|mmcblk ]]; then BOOTPART="${TARGET}p2"; ROOTPART="${TARGET}p3"
else BOOTPART="${TARGET}2"; ROOTPART="${TARGET}3"; fi

say "$(t i_fmt)..."
mkfs.fat -F 32 -n BOOT "$BOOTPART" || die "$(t f_fmt "$BOOTPART")"
mkfs.ext4 -F -L nixos "$ROOTPART" || die "$(t f_fmt "$ROOTPART")"

say "$(t i_mount)..."
mount "$ROOTPART" /mnt || die "$(t f_mount "$ROOTPART")"
mkdir -p /mnt/boot
mount "$BOOTPART" /mnt/boot || die "$(t f_mount "$BOOTPART")"

say "$(t i_hw)..."
nixos-generate-config --root /mnt || die "$(t f_hw)"

say "$(t i_copy)..."
mkdir -p /mnt/etc/snapos
mv /mnt/etc/nixos/hardware-configuration.nix /mnt/etc/snapos/
cp -a /etc/snapos-src/. /mnt/etc/snapos/
link_nixos

cat > /mnt/etc/snapos/local.nix <<EOF
{ ... }: {
  networking.hostName = "${HOSTNAME}";
  time.timeZone = "${TIMEZONE}";
  i18n.defaultLocale = "${LOCALE}";
  console.keyMap = "${KEYMAP}";
  snapos.appearance = "${LOOK}";
  users.users.${USERNAME} = {
    isNormalUser = true;
    extraGroups = [ "wheel" "networkmanager" ];
  };
  boot.loader.grub.device = "${TARGET}";
  boot.loader.grub.efiSupport = true;
  boot.loader.grub.efiInstallAsRemovable = true;
  boot.loader.efi.canTouchEfiVariables = false;
}
EOF
write_graphics
keep_network

printf '\n  %s%s%s\n\n' "$BLD" "$(t i_run)" "$RST"
LC_ALL=C.UTF-8 nixos-install --root /mnt --flake "path:/mnt/etc/snapos#snapos" --no-root-passwd \
    || die "$(t f_install)"

say "$(t i_pw "$USERNAME")..."
printf '%s:%s\n' "$USERNAME" "$PASSWORD" | LC_ALL=C.UTF-8 nixos-enter --root /mnt -c "chpasswd" \
    || warn "$(t i_pwfail)"
unset PASSWORD

printf '\n'
ok "${BLD}$(t done_title)${RST}"
restart
