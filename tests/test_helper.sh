#!/bin/sh
# KettleLAN – tests of the system helper (the only part that runs as root).
# Checks that malicious arguments are rejected before anything reaches the system.
#   tests/test_helper.sh <path to kettlelan-helper>
set -u
HELPER=${1:-scripts/kettlelan-helper}
fails=0
total=0

# a regular account with uid >= 1000 (on GitHub Actions: runner); without it some tests are skipped
UID_OK=$(getent passwd | awk -F: '$3 >= 1000 && $3 < 60000 { print $3; exit }')

expect() { # expect ok|fail description arguments...
    want=$1; desc=$2; shift 2
    total=$((total + 1))
    if out=$("$HELPER" "$@" 2>&1); then got=ok; else got=fail; fi
    if [ "$got" != "$want" ]; then
        fails=$((fails + 1))
        echo "  FAILED: $desc (expected $want, got $got: $out)"
    fi
}

echo "kettlelan-helper tests"
if [ -n "$UID_OK" ]; then
    expect ok   "valid input"                     check "$UID_OK" 100.80.46.79
    expect ok   "lower end of the range"             check "$UID_OK" 100.64.0.1
    expect ok   "upper end of the range"             check "$UID_OK" 100.127.255.254
    U=$UID_OK
else
    echo "  (no account with uid >= 1000 – skipping the valid-input tests)"
    U=1000
fi
# addresses outside the KettleLAN network
expect fail "address below the range"                 check "$U" 100.63.255.255
expect fail "address above the range"                 check "$U" 100.128.0.1
expect fail "home network address"                   check "$U" 192.168.8.1
expect fail "localhost"                             check "$U" 127.0.0.1
expect fail "octet above 255"                       check "$U" 100.80.256.1
expect fail "leading zeros (octal notation)"         check "$U" 100.80.046.79
expect fail "too few octets"                       check "$U" 100.80.1
expect fail "too many octets"                       check "$U" 100.80.1.1.1
expect fail "netmask in the address"                       check "$U" 100.80.1.1/8
expect fail "empty address"                           check "$U" ""
# command and option injection attempts
expect fail "semicolon and a command"                   check "$U" "100.80.1.1;reboot"
expect fail "\$(...) substitution"                  check "$U" '100.80.1.1$(id)'
expect fail "backticks"                    check "$U" '100.80.1.1`id`'
expect fail "newline in the address"                  check "$U" "100.80.1.1
reboot"
expect fail "space and an extra word"              check "$U" "100.80.1.1 dev lo"
expect fail "address as an option"                      check "$U" "-f"
expect fail "control bytes"            check "$U" "$(printf '100.80.1.1\033')"
# uid
expect fail "root uid"                              check 0 100.80.46.79
expect fail "system account uid"                 check 999 100.80.46.79
expect fail "non-existent uid"                     check 4294967294 100.80.46.79
expect fail "uid with letters"                        check abc 100.80.46.79
expect fail "negative uid"                            check -1 100.80.46.79
expect fail "uid with a space"                         check "1000 0" 100.80.46.79
expect fail "very long uid"                      check 100000000000000000000 100.80.46.79
expect fail "empty uid"                             check "" 100.80.46.79
# argument count and unknown commands
expect fail "too few arguments"                    check "$U"
expect fail "too many arguments"                    check "$U" 100.80.46.79 extra
expect fail "unknown command"                    rm -rf /
expect fail "down with an argument (without root)"         down kettle0
if [ "$(id -u)" -ne 0 ]; then
    expect fail "up without root"                      up "$U" 100.80.46.79
else
    echo "  (running as root – skipping \"up without root\" so no adapter is created)"
fi

echo "$total checks, $fails failed"
[ "$fails" -eq 0 ] && echo "All helper tests passed"
[ "$fails" -eq 0 ]
