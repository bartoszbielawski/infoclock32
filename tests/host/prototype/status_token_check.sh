#!/bin/bash
# push_token enforcement on the status endpoints. Usage: token.sh <base> <token>
H=$1; TOK=$2; fail=0
c() { curl -s -o /dev/null -m 15 -w "%{http_code}" "$@"; }
chk() { if [ "$2" = "$3" ]; then echo "ok   $1"; else echo "FAIL $1 :: got $2 want $3"; fail=1; fi; }
chk "no token: set"      "$(c "$H/status/set?text=x&minutes=1")" 403
chk "bad token: set"     "$(c "$H/status/set?text=x&minutes=1&token=nope")" 403
chk "no token: cancel"   "$(c "$H/status/cancel")" 403
chk "no token: info"     "$(c "$H/status/info")" 403
chk "no token: page"     "$(c "$H/s")" 403
chk "token: set"         "$(c "$H/status/set?text=x&minutes=1&token=$TOK")" 200
echo "token info: $(curl -s -m 15 "$H/status/info?token=$TOK")"
chk "token: page"        "$(c "$H/s?token=$TOK")" 200
chk "page keeps token in forms" "$(curl -s -m 15 "$H/s?token=$TOK" | grep -o "name='token' value='$TOK'" | wc -l)" 2
chk "ui redirect keeps token" "$(curl -s -o /dev/null -m 15 -w '%{redirect_url}' -X POST -d "ui=1&text=y&minutes=1&token=$TOK" "$H/status/set")" "$H/s?token=$TOK"
chk "token: cancel"      "$(c "$H/status/cancel?token=$TOK")" 200
chk "token with odd chars is escaped in redirect" "$(curl -s -o /dev/null -m 15 -w '%{http_code}' -X POST --data-urlencode "token=a<b>'c" -d "ui=1&text=y" "$H/status/set")" 403
exit $fail
