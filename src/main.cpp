#include <iostream>
#include <chrono>
#include <random>
#include <atomic>

#include <thread>
#include <mutex>
#include <semaphore>
#include <string>

// program constraints
#define STARTING_UNITS 75
#define N_BARISTAS 5
#define N_CUSTOMERS 100
#define SHELF_SPACES 5
#define QUEUE_SIZE 10
#define N_MACHINES 2

// global resource of the number of coffee units available
// in the coffee shop
int COFFEE_UNITS = STARTING_UNITS;
std::atomic<bool> shopClosed(false);

// ---------- printing ----------
std::mutex printLock;   // stops mixed lines

void print(std::string message) {
    printLock.lock();
    std::cout << message << std::endl;
    printLock.unlock();
}

// ---------- coffee stock ----------
std::mutex stockLock;   // check + take together

// true = got a unit
bool takeCoffeeUnit() {
    bool gotUnit = false;
    bool justClosed = false;

    stockLock.lock();
    if (COFFEE_UNITS > 0) {
        COFFEE_UNITS--;
        gotUnit = true;
        if (COFFEE_UNITS == 0) {
            shopClosed = true;   // last unit, close shop
            justClosed = true;
        }
    }
    stockLock.unlock();

    if (justClosed) {   // print outside lock
        print("*** Out of coffee - the shop is now closed ***");
    }
    return gotUnit;
}

// ---------- order queue ----------
class Orders {
private:
    int orderQueue[QUEUE_SIZE];   // circular array
    int in;                       // next write
    int out;                      // next read
    int count;                    // orders waiting

    std::mutex tillLock;          // the till
    std::counting_semaphore<QUEUE_SIZE> freeSlots;                // empty slots
    std::counting_semaphore<QUEUE_SIZE + N_BARISTAS> newOrders;   // orders + end wake-ups

public:
    Orders() : in(0), out(0), count(0), freeSlots(QUEUE_SIZE), newOrders(0) {
        for (int i = 0; i < QUEUE_SIZE; i++) {
            orderQueue[i] = -1;   // empty
        }
    }

    // false = shop closed
    bool placeOrder(int customerId) {
        freeSlots.acquire();   // wait for space first

        tillLock.lock();
        if (shopClosed) {
            tillLock.unlock();
            freeSlots.release();   // give slot back
            return false;
        }
        orderQueue[in] = customerId;
        in = (in + 1) % QUEUE_SIZE;
        count++;
        tillLock.unlock();

        newOrders.release();   // wake a barista
        return true;
    }

    // -1 = go home
    int takeOrder() {
        newOrders.acquire();   // wait for an order

        tillLock.lock();
        if (count == 0) {      // end wake-up
            tillLock.unlock();
            return -1;
        }
        int customerId = orderQueue[out];
        orderQueue[out] = -1;
        out = (out + 1) % QUEUE_SIZE;
        count--;
        tillLock.unlock();

        freeSlots.release();   // wake a customer
        return customerId;
    }

    // wake sleeping baristas
    void wakeBaristas() {
        newOrders.release(N_BARISTAS);
    }
};

// ---------- personal bell  ----------
struct CoffeeBell {
    std::binary_semaphore ring;

    CoffeeBell() : ring(0) {}   // starts silent
};

// ---------- pickup shelf ----------
class PickupShelf {
private:
    int shelf[SHELF_SPACES];   // customer IDs
    int itemCount;             // coffees on shelf

    std::mutex shelfLock;
    std::counting_semaphore<SHELF_SPACES> emptySpaces;
    CoffeeBell bells[N_CUSTOMERS];   // one per customer

public:
    PickupShelf() : itemCount(0), emptySpaces(SHELF_SPACES) {
        for (int i = 0; i < SHELF_SPACES; i++) {
            shelf[i] = -1;   // empty
        }
    }

    void placeCoffee(int customerId) {
        emptySpaces.acquire();   // wait if full

        shelfLock.lock();
        shelf[itemCount] = customerId;   // first empty slot
        itemCount++;
        shelfLock.unlock();

        bells[customerId].ring.release();   // ring after placing
    }

    // ring with no coffee
    void sendNoCoffee(int customerId) {
        bells[customerId].ring.release();
    }

    // false = no coffee
    bool collectCoffee(int customerId) {
        bells[customerId].ring.acquire();   // wait for my bell

        shelfLock.lock();
        int position = 0;
        while (position < itemCount && shelf[position] != customerId) {
            position++;
        }
        if (position == itemCount) {   // not on shelf
            shelfLock.unlock();
            return false;              // no space freed
        }
        for (int i = position; i < itemCount - 1; i++) {
            shelf[i] = shelf[i + 1];   // shift down
        }
        itemCount--;
        shelf[itemCount] = -1;
        shelfLock.unlock();

        emptySpaces.release();   // space for a barista
        return true;
    }
};

// ---------- coffee machines ----------
class CoffeeMachine {
private:
    bool inUse[N_MACHINES];   // which are busy

    std::mutex machineLock;
    std::counting_semaphore<N_MACHINES> available;   // free machines

public:
    CoffeeMachine() : available(N_MACHINES) {
        for (int i = 0; i < N_MACHINES; i++) {
            inUse[i] = false;
        }
    }

    // returns machine number
    int acquireMachine() {
        available.acquire();   // wait for a machine

        machineLock.lock();
        int machine = 0;
        while (inUse[machine]) {   // one is surely free
            machine++;
        }
        inUse[machine] = true;
        machineLock.unlock();

        return machine;
    }

    void releaseMachine(int machine) {
        machineLock.lock();
        inUse[machine] = false;   // free before waking
        machineLock.unlock();

        available.release();
    }
};

// ---------- barista ----------
class Barista {
private:
    int id;
    Orders* orders;            // shared, not copies
    PickupShelf* shelf;
    CoffeeMachine* machines;

public:
    void setup(int baristaId, Orders* sharedOrders, PickupShelf* sharedShelf,
               CoffeeMachine* sharedMachines) {
        id = baristaId;
        orders = sharedOrders;
        shelf = sharedShelf;
        machines = sharedMachines;
    }

    void run() {
        std::random_device rd;   // own random, no lock

        while (true) {
            int customerId = orders->takeOrder();
            if (customerId == -1) {
                print("Barista " + std::to_string(id) + " goes home");
                break;
            }

            int machine = machines->acquireMachine();

            bool gotUnit = takeCoffeeUnit();   // after machine 
            if (!gotUnit) {
                machines->releaseMachine(machine);
                shelf->sendNoCoffee(customerId);
                continue;   // empty the queue
            }

            print("Barista " + std::to_string(id) + " is making coffee for customer "
                  + std::to_string(customerId) + " on machine " + std::to_string(machine));
            unsigned int makeTime = 1000 + rd() % 9001;   // 1-10 seconds
            std::this_thread::sleep_for(std::chrono::milliseconds(makeTime));

            machines->releaseMachine(machine);   // before the shelf
            shelf->placeCoffee(customerId);
        }
    }
};

// ---------- customer ----------
class Customer {
private:
    int id;
    Orders* orders;
    PickupShelf* shelf;
    int result;   // 0 closed, 1 coffee, 2 none

public:
    void setup(int customerId, Orders* sharedOrders, PickupShelf* sharedShelf) {
        id = customerId;
        orders = sharedOrders;
        shelf = sharedShelf;
        result = 0;
    }

    // read after join
    int getResult() {
        return result;
    }

    void run() {
        std::random_device rd;

        unsigned int arriveDelay = rd() % 10001;   // 0-10 seconds
        std::this_thread::sleep_for(std::chrono::milliseconds(arriveDelay));

        bool ordered = orders->placeOrder(id);
        if (!ordered) {
            result = 0;
            print("Customer " + std::to_string(id) + " left (the shop is closed)");
            return;
        }
        print("Customer " + std::to_string(id) + " placed an order");

        bool gotCoffee = shelf->collectCoffee(id);
        if (gotCoffee) {
            result = 1;
            print("Customer " + std::to_string(id) + " got their coffee and left");
        } else {
            result = 2;
            print("Customer " + std::to_string(id) + " left without coffee (no coffee left)");
        }
    }
};

// ---------- thread entry points ----------
void baristaThread(Barista* barista) {
    barista->run();
}

void customerThread(Customer* customer) {
    customer->run();
}

int main() {
    // setup the program
    PickupShelf shelf = PickupShelf();
    Orders orders = Orders();
    CoffeeMachine machines = CoffeeMachine();

    Barista baristas[N_BARISTAS];
    Customer customers[N_CUSTOMERS];
    std::thread baristaThreads[N_BARISTAS];
    std::thread customerThreads[N_CUSTOMERS];

    for (int i = 0; i < N_BARISTAS; i++) {
        // create barista threads
        baristas[i].setup(i, &orders, &shelf, &machines);
        baristaThreads[i] = std::thread(baristaThread, &baristas[i]);
    }

    for (int i = 0; i < N_CUSTOMERS; i++) {
        // create customer threads
        customers[i].setup(i, &orders, &shelf);
        customerThreads[i] = std::thread(customerThread, &customers[i]);
    }

    for (int i = 0; i < N_CUSTOMERS; i++) {
        customerThreads[i].join();
    }

    // customers gone, free baristas
    shopClosed = true;
    orders.wakeBaristas();

    for (int i = 0; i < N_BARISTAS; i++) {
        baristaThreads[i].join();
    }

    // count results
    int gotCoffee = 0;
    int noCoffee = 0;
    int shopWasClosed = 0;
    for (int i = 0; i < N_CUSTOMERS; i++) {
        if (customers[i].getResult() == 1) {
            gotCoffee++;
        } else if (customers[i].getResult() == 2) {
            noCoffee++;
        } else {
            shopWasClosed++;
        }
    }

    std::cout << std::endl;
    std::cout << "===== End of the day =====" << std::endl;
    std::cout << "Customers who got coffee:              " << gotCoffee << std::endl;
    std::cout << "Customers who ordered but got none:    " << noCoffee << std::endl;
    std::cout << "Customers who found the shop closed:   " << shopWasClosed << std::endl;
    std::cout << "Coffee units used:                     " << STARTING_UNITS - COFFEE_UNITS << std::endl;
    std::cout << "Coffee units left (COFFEE_UNITS):      " << COFFEE_UNITS << std::endl;

    return 0;
}
