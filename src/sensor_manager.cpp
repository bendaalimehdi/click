#include "sensor_manager.h"

SensorManager::SensorManager(int sda, int scl, const String& sensorType)
    : _sda(sda), _scl(scl), _sensorType(sensorType) {}

bool SensorManager::begin() {
    // Solution universelle pour éviter le double "Wire.begin()" :
    // On utilise une variable statique locale. Elle survit entre les appels
    // mais reste confinée à cette fonction.
    static bool isWireStarted = false;

    if (!isWireStarted) {
        if (Wire.begin(_sda, _scl)) {
            isWireStarted = true;
        } else {
            _lastError = "Failed to initialize I2C bus";
            return false;
        }
    }

    if (_sensorType == "bmp280") {
        // Tentative sur l'adresse 0x76
        // Note: Certaines libs Adafruit appellent Wire.begin() en interne.
        // Si le warning "Bus already started" apparaît encore, c'est interne à la lib
        // et n'est pas bloquant pour votre code.
        if (!_bmp.begin(0x76)) {
            // Tentative de secours sur l'adresse 0x77
            if (!_bmp.begin(0x77)) {
                _lastError = "BMP280 not found on 0x76 or 0x77";
                _ready = false;
                return false;
            }
        }

        _bmp.setSampling(
            Adafruit_BMP280::MODE_FORCED,
            Adafruit_BMP280::SAMPLING_X1,
            Adafruit_BMP280::SAMPLING_NONE,
            Adafruit_BMP280::FILTER_OFF,
            Adafruit_BMP280::STANDBY_MS_1
        );

        _ready = true;
        return true;
    }

    _lastError = "Unsupported sensor_type: " + _sensorType;
    _ready = false;
    return false;
}

float SensorManager::readTemperatureC() {
    if (!_ready) return NAN;

    if (_sensorType == "bmp280") {
        _bmp.takeForcedMeasurement();
        return _bmp.readTemperature();
    }

    return NAN;
}

String SensorManager::getLastError() const {
    return _lastError;
}