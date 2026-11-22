# Coffee Shop: a concurrency simulation in C++20

A multithreaded coffee shop simulation. 100 customer threads and 5 barista threads share a small order queue, two coffee machines, a pickup shelf and a limited stock of coffee. The shop closes when the coffee runs out, and the program has to finish cleanly every time: no deadlock, no thread left waiting forever, no lost or double-counted coffee.

Written with the standard library only (`std::thread`, `std::mutex`, `std::counting_semaphore`, `std::binary_semaphore`).

## The model

| Resource | Size | Where |
| --- | --- | --- |
| Customers (threads) | 100 | `Customer` |
| Baristas (threads) | 5 | `Barista` |
| Order queue | 10 places | `Orders` |
| Coffee machines | 2 | `CoffeeMachine` |
| Pickup shelf | 5 places | `PickupShelf` |
| Coffee units in stock | 75 | `takeCoffeeUnit()` |

A customer arrives after a random delay, places an order, then waits for their personal bell. A barista takes an order, uses a free machine, takes one unit of coffee, brews for 1-10 seconds, puts the cup on the shelf and rings the customer's bell. When the last unit is taken the shop closes: customers who have not ordered yet leave, and customers whose order is still queued are told there is no coffee.

## How it works

| Problem | Solution |
| --- | --- |
| Bounded order queue (producer/consumer) | Circular array, two counting semaphores (`freeSlots`, `newOrders`) and one mutex for the array itself |
| Stock must never go below zero | Check and decrement happen together under one mutex; the thread that takes the last unit closes the shop |
| At most 2 coffees brewing at once | Counting semaphore for the number of free machines, a mutex only to pick which one |
| Shelf has 5 places | Counting semaphore (`emptySpaces`); a barista waits for space before placing a cup |
| Each customer must get exactly their own answer | One `binary_semaphore` "bell" per customer: rung after the cup is placed, or rung with no coffee if the stock ran out |
| Clean shutdown | When all customers are done, `main` sets the closed flag and releases extra permits to wake sleeping baristas; they see an empty queue and go home |

## Why it does not deadlock

- **Blocking waits happen on semaphores, never while holding a mutex.** Every mutex is held only for a few lines of code, and no thread ever holds two mutexes at once.
- **A barista releases the machine before putting the cup on the shelf.** Otherwise a barista waiting for shelf space could keep a machine, and the customers who would free the shelf could be waiting behind it.
- **Every blocked customer has a way out.** Each customer waits on their own bell, and a bell is always rung exactly once: with a coffee, or without one.
- **Queue slots are handed back.** A customer who finds the shop closed after waiting for a slot returns the slot, so no one else stays blocked.
- **Wake-ups at the end are counted.** The queue semaphore is sized for `QUEUE_SIZE + N_BARISTAS` so the final wake-up permits always fit.

## Build and run

```bash
make          # builds build/cafe (g++, C++20, -Wall -Wextra -pedantic)
make run      # full-speed run, takes about 3-4 minutes because every coffee takes 1-10 seconds
```

Needs a C++20 compiler (GCC 11 or newer, or a recent Clang) and `make`.

## Tests

The simulation is random and slow, so the checks run a copy of the same code with the delays shortened 1000x (milliseconds become microseconds):

```bash
make stress                # 100 fast runs
make sanitize              # ThreadSanitizer, then AddressSanitizer + UBSan
```

Each run must finish (no deadlock), the three customer counts must add up to 100, used + remaining units must equal 75 (and all 75 must be used), and every unit that was used must end up with a customer. The same checks run in GitHub Actions on every push.

## Sample output

Counts vary from run to run because arrival and brewing times are random. With 100 customers and only 75 units, every run ends with all 75 coffees served:

```
*** Out of coffee - the shop is now closed ***
Barista 3 goes home
Barista 4 goes home

===== End of the day =====
Customers who got coffee:              75
Customers who ordered but got none:    12
Customers who found the shop closed:   13
Coffee units used:                     75
Coffee units left (COFFEE_UNITS):      0
```

## Project layout

```
src/main.cpp          the whole simulation
scripts/stress.sh     fast repeated runs with correctness checks
Makefile              build, run, stress, sanitize
.github/workflows/    CI
```
