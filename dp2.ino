#include <Wire.h>
#include <SparkFun_APDS9960.h>
#include <DFRobot_I2CMultiplexer.h>

// ---------------- Helper types -------------------
typedef enum {
    INITIAL,
    WAITING,
    TRANSITION
} state_t;

typedef enum {
    CORRECT,
    INCORRECT,
    UNKNOWN
} sensor_reading_t;

typedef enum {
    RED,
    GREEN,
    BLUE,
    NONE
} color_t;

typedef struct {
    uint16_t r;
    uint16_t g;
    uint16_t b;
} rgb_t;

// ------------------ Constants -----------------------
const int INITIAL_ROUND_DURATION = 5000;
const int ROUND_DURATION_MIN = 100;
const int INITIAL_DURATION_DELTA = 400;
const int DURATION_DELTA_MIN = 50;
const double DURATION_EXPONENT = 0.90;

const unsigned long ROUND_TRANSITION_DURATION = 1500;
const int GAME_DIVISIONS = 4;

const int COLOR_DELTA_THRESH = 10;
const int R_RETRY_THRESH = 100;

// ----------------- Pin numbers ----------------------
const int LIGHT_PINS[] = {7, 8};

const int SPEAKER_PIN = A1;
const int BUTTON_PIN = 6;
const int LED_PINS[] = {5, 4, 3, 2};

const int I2C_PORTS[] = {0, 1, 2, 3};

// ----------------- RGB controllers ------------------
SparkFun_APDS9960 APDS = SparkFun_APDS9960();
DFRobot_I2CMultiplexer I2C_MULTI(0x70);

// ------------------ Game state -----------------------
rgb_t ambient_rgb[] = {{}, {}, {}, {}};

long r_seed;
bool has_started = false;

// State machine variables
state_t state = INITIAL;
unsigned long cur_round_duration = INITIAL_ROUND_DURATION;
unsigned long cur_duration_delta = INITIAL_DURATION_DELTA;

unsigned long last_state_timestamp;
color_t color_settings[] = {NONE, NONE, NONE, NONE};

// ----------------- Support functions ----------------
// Marsaglia 32 bit xor-shift for better randomness
long xor_shift() {
    r_seed ^= r_seed << 13;
    r_seed ^= r_seed >> 7;
    r_seed ^= r_seed << 17;
    return r_seed > 0 ? r_seed : -r_seed;
}

void debug_rgb(rgb_t rgb) {
    Serial.print(rgb.r);
    Serial.print(" ");
    Serial.print(rgb.g);
    Serial.print(" ");
    Serial.println(rgb.b);
}

// -------------- RGB sensor wrappers --------------
bool read_rgb(int port, rgb_t rgb) {
    /* I2C_MULTI.selectPort(port);
    if (!APDS.readRedLight(rgb.r) ||
        !APDS.readGreenLight(rgb.g) ||
        !APDS.readBlueLight(rgb.b)) {
        Serial.println("Error reading light values");
        return false;
    } */

    return true;
}

color_t decode_color(rgb_t rgb) {
    if (rgb.r < COLOR_DELTA_THRESH
        && rgb.g < COLOR_DELTA_THRESH
        && rgb.b < COLOR_DELTA_THRESH) {
        return NONE;
    }

    if (rgb.r > rgb.g && rgb.r > rgb.b) {
        return RED;
    }

    if (rgb.g > rgb.b && rgb.g > rgb.r) {
        return GREEN;
    }

    if (rgb.b > rgb.g && rgb.b > rgb.r) {
        return BLUE;
    }

    return NONE;
}

void write_color(int idx, color_t color) {
    // TODO: Set RGB color at the given index
}

// ------------ State helper functions -------------
void randomize() {
    color_t available_colors[] = {RED, GREEN, BLUE};
    int available_colors_len = 3;

    for (int i = 0; i < 3; ++i) {
        int r;
        color_t color;
        int tries = 0;
        do {
            r = xor_shift() % available_colors_len;
            color = available_colors[r];

            tries++;

            // Guard against poor xorshift sequence
            if (tries == R_RETRY_THRESH) {
                for (color_t c : available_colors) {
                    if (c != NONE) {
                        color = c;
                    }
                }
            }
        } while (color == NONE);

        available_colors_len--;
        available_colors[i] = color;
        
        color_settings[i] = color;
        write_color(i, color);
    }
}

sensor_reading_t can_waiting_state_proceed() {
    int correct_readings = 0;
    for (int port : I2C_PORTS) {
        rgb_t rgb = {};
        if (read_rgb(port, rgb)) {
            rgb_t ambient = ambient_rgb[port];
            rgb_t delta_rgb = {rgb.r - ambient.r, rgb.g - ambient.g, rgb.b - ambient.b};
            color_t reading = decode_color(delta_rgb);

            if (reading != NONE) {
                color_t setting = color_settings[port];
                
                // Short circuit if it's the wrong card
                if (setting != reading) {
                    return INCORRECT;
                } else {
                    correct_readings++;
                }
            }
        }
    }

    return correct_readings == 2 ? CORRECT : UNKNOWN;
}

void clear_state() {
    for (int i = 0; i < GAME_DIVISIONS; ++i) {
        color_settings[i] = NONE;
        write_color(i, NONE);
    }
}

void reset_game() {
    clear_state();
  
    state = INITIAL;
    cur_round_duration = INITIAL_ROUND_DURATION;
    cur_duration_delta = INITIAL_DURATION_DELTA;
  
    delay(2000);
}

// ------------- Round begin/end functions ---------------
void play_begin_round_tone() {
    /* tone(SPEAKER_PIN, 4000, 500); */
    Serial.println("START");
}

void play_correct_tone() {
    /* tone(SPEAKER_PIN, 4000, 500);
    delay(200);
    tone(SPEAKER_PIN, 4500, 500); */
    Serial.println("CORRECT");
}

void play_incorrect_tone() {
    /* tone(SPEAKER_PIN, 1000, 1000);
    delay(200);
    tone(SPEAKER_PIN, 1000, 500); */
    Serial.println("INCORRECT");
}

// ---------------- Arduino control ------------------
void setup() {
    Serial.begin(9600);
    
    pinMode(SPEAKER_PIN, OUTPUT);
    pinMode(BUTTON_PIN, INPUT);
    for (int pin : LED_PINS) {
        pinMode(pin, OUTPUT);
    }

    // Turn on the light LEDs
    for (int pin : LIGHT_PINS) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }

    // Initialize RGB sensors
    for (int port : I2C_PORTS) {
        I2C_MULTI.selectPort(port);
        if (!APDS.init()) {
            Serial.println("Failed to initialize the RGB sensor");
        }

        if (!APDS.enableLightSensor(false)) {
            Serial.println("Failed to initialize the RGB sensor");
        }

        // Wait for sensor calibration
        delay(500);

        // Read ambient light RGB values
        read_rgb(port, ambient_rgb[port]);
    }
    
    // Setup time seems to be "relatively" good at
    // adding entropy to the random seed
    r_seed = millis();
}

void loop() {
    // Await button press
    int button_reading = digitalRead(BUTTON_PIN);
    if (button_reading == HIGH) {
        has_started = true;

        reset_game();
    }

    if (!has_started) {
        return;
    }

    unsigned long cur_time = millis();
    if (state == INITIAL) {
        randomize();
        play_begin_round_tone();

        last_state_timestamp = cur_time;
        state = WAITING;
    } else if (state == WAITING) {
        sensor_reading_t sr = can_waiting_state_proceed();

        // Wrong card(s), move on
        if (sr == INCORRECT) {
            play_incorrect_tone();
        }

        // All correct cards, move on
        if (sr == CORRECT) {
            play_correct_tone();
        }

        // Still waiting for them to put in another card
        if (sr == UNKNOWN) {
            unsigned long elapsed = cur_time - last_state_timestamp;
            if (elapsed > cur_round_duration) {
                play_incorrect_tone();
            } else {
                return;
            }
        }

        state = TRANSITION;
    } else if (state == TRANSITION) {
        clear_state();

        if (cur_round_duration > ROUND_DURATION_MIN) {
            cur_round_duration -= cur_duration_delta;

            if (cur_duration_delta > DURATION_DELTA_MIN) {
                cur_duration_delta = (int) (cur_duration_delta * DURATION_EXPONENT);
            }
        }

        delay(ROUND_TRANSITION_DURATION);
        state = INITIAL;
    }
}
