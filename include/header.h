#define SELECTPIN 0
#define W1PIN     6
#define W2PIN     7
#define W3PIN     8
#define W4PIN     9
#define SUPPORTEDWHEELS 50
#define NUMWHEELS 4
#define NSIZE     80
#define NPAGES    5
// Active wheel structure stores the currently selected wheel name/number
struct activeWheel {
  char name[NSIZE] = "Intens";
  int num = 1;
};

// Page structure holds one page of active weels
struct page {
  activeWheel activeWheels[NUMWHEELS];
};

struct Config {
  page pages[NPAGES];
};
int getPadding(int size, const char *msg);
void printMessage(int row, int slot, int msg, boolean center = true);
void printMessage(int row, int slot, const char *msg, boolean center);
int parseOSCMessage(char *msg, int len);
void printWheelValue(int i, int value);
void printWheelName(int num);
void loadConfiguration(const char *filename, Config &config);
void saveConfiguration(const char *filename, const Config &config);