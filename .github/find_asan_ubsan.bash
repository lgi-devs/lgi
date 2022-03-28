sanitizers="$(ldd "$1" | awk -vORS=: '$1 ~ /lib(a|ub)san/ { print $3 ":" }')"
export sanitizers
