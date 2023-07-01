#include <WiFi.h>
#include <IPAddress.h>
#include <HTTPClient.h>

#include <Adafruit_Fingerprint.h>
#include <ESP32Servo.h>

#define MAX_LISTENERS 20

#define WIFI_SSID "IFMG_Servidores"
#define WIFI_PASSWORD NULL

#define WIFI_LED 2
#define RED_LED 18
#define GREEN_LED 22

#define LOCK_SERVO_PORT 26
#define LOCK_SERVO_OPENED 100 // ELE NO TOPO
#define LOCK_SERVO_CLOSED 190 // ELE PRA BAIXO

#define PUSH_BUTTON 15

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&Serial2);
Servo lockServo;

TaskHandle_t WifiTask;

typedef void (*Callable)();

typedef struct
{
  Callable function;
  int interval;
  int execute;
  bool repeat;
} Schedule;

Schedule schedules[MAX_LISTENERS];

void open()
{
  lockServo.write(LOCK_SERVO_OPENED);
  digitalWrite(GREEN_LED, HIGH);
  addSchedule(close, 5, false);
}

void disableAlert()
{
  digitalWrite(RED_LED, LOW);
}

void alert()
{
  digitalWrite(RED_LED, HIGH);
  addSchedule(disableAlert, 5, false);
}

void close()
{
  lockServo.write(LOCK_SERVO_CLOSED);
  digitalWrite(GREEN_LED, LOW);
}

void fingerManager()
{
  int result = finger.getImage();
  if (result == FINGERPRINT_OK)
  {
    Serial.println(F("Nova ocorrencia de dedo!"));
    if (finger.image2Tz() != FINGERPRINT_OK)
    {
      Serial.println(F("Erro na conversão da imagem!"));
      return;
    }

    if (finger.fingerFastSearch() != FINGERPRINT_OK)
    {
      Serial.println(F("Dedo não encontrado!"));
      alert();
      return;
    }

    sendRequest(getName(finger.fingerID), true /* VERIFICAR PUSH BUTTON */, 308);
    Serial.println("Digital de " + getName(finger.fingerID) + " encontrada!");

    open();

    return;
  }
}

bool sendRequest(String name, bool took, int port)
{
  HTTPClient http;

  http.begin("https://docs.google.com/forms/d/e/1FAIpQLSczsF3e2sIlJW1pv5M4xh0arTnAgQu9jG1RlhVZYEeMOv-adA/formResponse?ifq&entry.669983157=" + name + "&entry.1164638721=" + (took ? "Pegou" : "Guardou") + "%20a%20chave%20" + String(port) + "&submit=Submit");

  int httpCode = http.GET();

  Serial.printf("[HTTP] Status Code: %d\n", httpCode);

  if (httpCode <= 0)
  {
    Serial.printf("[HTTP] A requisição GET falou, erro: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
  return true;
}

String getName(int number)
{
  String nomes[] = {
      "Miguel%20Pinheiro",
      "Miguel%20BOB",
      "Raissa",
      "Roniery",
      "Leandro",
      "Gustavo",
      "Julia"};
  if (number > ((sizeof(nomes) / sizeof(nomes[0])) - 1) || number < 1)
  {
    return "Nao%20Registrado";
  }
  return nomes[number - 1];
}

void setup()
{
  Serial.begin(115200);
  Serial.println(F("Inicializando"));

  xTaskCreatePinnedToCore(wifiHandler, "wifiHandler", 4096, NULL, 1, &WifiTask, 0);

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  Serial.println(F("Iniciando sensor de impressão digital..."));
  finger.begin(57600);
  if (finger.verifyPassword())
  {
    Serial.println(F("Sensor de impressão digital encontrado!"));
  }
  else
  {
    Serial.println(F("Não foi possível encontrar o sensor de impressão digital :("));
    return;
  }

  Serial.println(F("Iniciando servo..."));

  lockServo.attach(LOCK_SERVO_PORT);
  lockServo.write(LOCK_SERVO_CLOSED);

  Serial.println(F("OK!"));
}

void loop()
{
  fingerManager();
  schedule(); // ATUALIZAR TAREFAS
  delay(10);
}

void wifiHandler(void *parameter)
{
  pinMode(WIFI_LED, OUTPUT);

  Serial.print("Conectando-se ao Wi-Fi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(100);
    Serial.print(".");
  }
  Serial.println(" Conectado!");

  while (true)
  {
    switch (WiFi.status())
    {
    case WL_CONNECTED:
      digitalWrite(WIFI_LED, HIGH);
      break;
    case WL_NO_SSID_AVAIL:
      Serial.println(F("Rede não encontrada"));
      break;
    case WL_CONNECT_FAILED:
    case WL_CONNECTION_LOST:
    case WL_DISCONNECTED:
      digitalWrite(WIFI_LED, LOW);
      Serial.println(F("Reconectando..."));
      // WiFi.reconnect();
      break;
    default:
      Serial.println(F("Rede ociosa..."));
      break;
    }
    delay(1000);
  }
}

void addSchedule(
    Callable function,
    int interval,
    bool repeat)
{
  const int now = millis();
  const Schedule newSchedule = {function, interval, (interval * 1000) + now, repeat};

  for (int i = 0; i < MAX_LISTENERS; i++)
  {
    const Schedule currentSchedule = schedules[i];

    if (currentSchedule.interval == 0 || currentSchedule.execute <= now)
    {
      schedules[i] = newSchedule;
      return;
    }
  }

  Serial.println("O número de MAX_LISTENERS foi excedido. Caso necessário aumente o número!");
}

void schedule()
{
  int now = millis();

  for (int i = 0; i < MAX_LISTENERS; i++)
  {
    const Schedule currentSchedule = schedules[i];
    if (currentSchedule.interval && currentSchedule.execute <= now)
    {
      currentSchedule.function(); // TODO: ADD ARGUMENTS
      schedules[i] = {};
      if (currentSchedule.repeat)
      {
        addSchedule(
            currentSchedule.function,
            currentSchedule.interval,
            currentSchedule.repeat);
        return;
      }
    }
  }
}