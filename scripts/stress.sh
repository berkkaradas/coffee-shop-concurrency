#!/usr/bin/env bash
# Runs the simulation many times with the delays shortened (1000x) and checks that
#   - every run finishes (no deadlock, no lost wake-up)
#   - the three customer counts add up to the number of customers
#   - used + left coffee units equals the starting stock, and all of it is used
#   - every unit that was used ended up in a customer's cup
#
# usage: scripts/stress.sh [runs] [plain|thread|address]
set -u

RUNS=${1:-100}
MODE=${2:-plain}
SRC="$(dirname "$0")/../src/main.cpp"
BUILD="$(dirname "$0")/../build"
mkdir -p "$BUILD"

# microseconds instead of milliseconds: same logic, runs in a fraction of a second
sed 's/std::chrono::milliseconds(/std::chrono::microseconds(/g' "$SRC" > "$BUILD/main_fast.cpp"

case "$MODE" in
  thread)  SAN="-fsanitize=thread" ;;
  address) SAN="-fsanitize=address,undefined" ;;
  *)       SAN="" ;;
esac

${CXX:-g++} -std=c++20 -O1 -g -pthread $SAN "$BUILD/main_fast.cpp" -o "$BUILD/cafe_fast" || exit 1

hangs=0
fails=0
for i in $(seq 1 "$RUNS"); do
  out=$(timeout 30 "$BUILD/cafe_fast" 2>&1)
  rc=$?
  if [ $rc -eq 124 ]; then
    hangs=$((hangs + 1)); echo "run $i: did not finish (possible deadlock)"; continue
  fi
  if [ $rc -ne 0 ] || echo "$out" | grep -q -E "WARNING: ThreadSanitizer|ERROR: AddressSanitizer|runtime error"; then
    fails=$((fails + 1)); echo "run $i: failed (exit code $rc)"; echo "$out" | tail -15; continue
  fi
  got=$(echo "$out"  | awk '/got coffee:/{print $NF}')
  none=$(echo "$out" | awk '/ordered but got none:/{print $NF}')
  shut=$(echo "$out" | awk '/found the shop closed:/{print $NF}')
  used=$(echo "$out" | awk '/Coffee units used:/{print $NF}')
  left=$(echo "$out" | awk '/Coffee units left/{print $NF}')
  if [ $((got + none + shut)) -ne 100 ] || [ $((used + left)) -ne 75 ] || [ "$used" -ne 75 ] || [ "$got" -ne "$used" ]; then
    fails=$((fails + 1))
    echo "run $i: counts do not add up (got=$got none=$none closed=$shut used=$used left=$left)"
  fi
done

echo "mode=$MODE runs=$RUNS hangs=$hangs failures=$fails"
[ "$hangs" -eq 0 ] && [ "$fails" -eq 0 ]
