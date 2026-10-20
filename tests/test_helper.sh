#!/bin/sh
# KettleLAN – testy pomocnika systemowego (jedynego elementu działającego jako root).
# Sprawdza, że złośliwe argumenty są odrzucane, zanim cokolwiek trafi do systemu.
#   tests/test_helper.sh <ścieżka do kettlelan-helper>
set -u
HELPER=${1:-scripts/kettlelan-helper}
fails=0
total=0

# zwykłe konto z uid >= 1000 (na GitHub Actions: runner); bez niego część testów jest pomijana
UID_OK=$(getent passwd | awk -F: '$3 >= 1000 && $3 < 60000 { print $3; exit }')

expect() { # expect ok|fail opis argumenty...
    want=$1; desc=$2; shift 2
    total=$((total + 1))
    if out=$("$HELPER" "$@" 2>&1); then got=ok; else got=fail; fi
    if [ "$got" != "$want" ]; then
        fails=$((fails + 1))
        echo "  NIEUDANE: $desc (oczekiwano $want, wynik $got: $out)"
    fi
}

echo "Testy pomocnika kettlelan-helper"
if [ -n "$UID_OK" ]; then
    expect ok   "poprawne dane"                     check "$UID_OK" 100.80.46.79
    expect ok   "dolna granica zakresu"             check "$UID_OK" 100.64.0.1
    expect ok   "górna granica zakresu"             check "$UID_OK" 100.127.255.254
    U=$UID_OK
else
    echo "  (brak konta z uid >= 1000 – pomijam testy poprawnych danych)"
    U=1000
fi
# adres spoza sieci KettleLAN
expect fail "adres poniżej zakresu"                 check "$U" 100.63.255.255
expect fail "adres powyżej zakresu"                 check "$U" 100.128.0.1
expect fail "adres sieci domowej"                   check "$U" 192.168.8.1
expect fail "localhost"                             check "$U" 127.0.0.1
expect fail "oktet ponad 255"                       check "$U" 100.80.256.1
expect fail "zera wiodące (zapis ósemkowy)"         check "$U" 100.80.046.79
expect fail "za mało oktetów"                       check "$U" 100.80.1
expect fail "za dużo oktetów"                       check "$U" 100.80.1.1.1
expect fail "maska w adresie"                       check "$U" 100.80.1.1/8
expect fail "pusty adres"                           check "$U" ""
# próby wstrzyknięcia poleceń i opcji
expect fail "średnik i polecenie"                   check "$U" "100.80.1.1;reboot"
expect fail "podstawienie \$(...)"                  check "$U" '100.80.1.1$(id)'
expect fail "odwrotne apostrofy"                    check "$U" '100.80.1.1`id`'
expect fail "nowa linia w adresie"                  check "$U" "100.80.1.1
reboot"
expect fail "spacja i dodatkowe słowo"              check "$U" "100.80.1.1 dev lo"
expect fail "adres jako opcja"                      check "$U" "-f"
expect fail "znak NUL / bajty sterujące"            check "$U" "$(printf '100.80.1.1\033')"
# uid
expect fail "uid root"                              check 0 100.80.46.79
expect fail "uid konta systemowego"                 check 999 100.80.46.79
expect fail "uid nieistniejący"                     check 4294967294 100.80.46.79
expect fail "uid z literami"                        check abc 100.80.46.79
expect fail "uid ujemny"                            check -1 100.80.46.79
expect fail "uid ze spacją"                         check "1000 0" 100.80.46.79
expect fail "uid bardzo długi"                      check 100000000000000000000 100.80.46.79
expect fail "uid pusty"                             check "" 100.80.46.79
# liczba argumentów i nieznane polecenia
expect fail "za mało argumentów"                    check "$U"
expect fail "za dużo argumentów"                    check "$U" 100.80.46.79 extra
expect fail "nieznane polecenie"                    rm -rf /
expect fail "down z argumentem (bez roota)"         down kettle0
if [ "$(id -u)" -ne 0 ]; then
    expect fail "up bez roota"                      up "$U" 100.80.46.79
else
    echo "  (uruchomiono jako root – pomijam test „up bez roota”, żeby nie tworzyć karty)"
fi

echo "$total sprawdzeń, $fails nieudanych"
[ "$fails" -eq 0 ] && echo "Wszystkie testy pomocnika OK"
[ "$fails" -eq 0 ]
