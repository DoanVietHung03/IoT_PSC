#include <WiFi.h>
#include <PubSubClient.h>
#include "DHTesp.h"
#include <ESP32Servo.h>
#include "HX711.h"
#include <math.h>
#include "ThingSpeak.h"
#include <Wire.h>              
#include "LiquidCrystal_I2C_Hangul.h" 

LiquidCrystal_I2C_Hangul lcd(0x27, 20, 4); // Set the LCD I2C address to 0x27 for a 16x2 display
//Khai báo kí tự độ ('°')
uint8_t degreeSymbol[8] = {
  0b00110,
  0b01001,
  0b01001,
  0b00110,
  0b00000,
  0b00000,
  0b00000,
  0b00000
};

//ThingSpeakSetting
unsigned long myChannelNumber = 2238550; // Mã channel ThingSpeak
const char * myWriteAPIKey = "6TH3QEWEL1U8XS2X"; // Mã API Key ThingSpeak 

//Khai báo cảm biến trọng lượng
HX711 food; //Thức ăn
HX711 water; //Nước uống
HX711 stored_food; //Thức ăn dự trữ
HX711 stored_water; //Nước uống dự trữ

//Khai báo 2 cổng phát tín hiệu của servo và 2 servo đóng mở ống thả nước và thức ăn
#define servo_water_Pin 12 //Nước uống
#define servo_food_Pin 13 //Thức ăn
Servo servo_water, servo_food; //Khai báo 2 servo


//Khai báo các biến toàn cục khác
int Machine_status = 0; // Máy hoạt động nếu = 1, máy không hoạt động nếu = 0
int lastState = LOW; //Biến lưu giá trị thay đổi gần nhất của button 
int lastMillis = 0; //Biến lưu thời gian bắt đầu thay đổi của button
int Misting = 0; //Biến lưu giá trị theo dõi phun sương

//Khai báo các biến hỗ trợ kết nối Wifi
const char* ssid = "Wokwi-GUEST";
const char* password = "";

//Khai báo cảm biến nhiệt độ - độ ẩm
const int DHT_PIN = 15;
DHTesp dhtSensor;

//***Set server***
const char* mqttServer = "broker.hivemq.com"; 
int port = 1883;

//Khai báo các biến hỗ trợ IFTTT
const char* host = "maker.ifttt.com";
const int httpport = 80;
const char* food_request = "/trigger/outoffood/with/key/cCph2UD9XdIyKY1nj0-4AGbx6hLqby_yG2y8ukq3uIC";
const char* water_request = "/trigger/outofwater/with/key/cCph2UD9XdIyKY1nj0-4AGbx6hLqby_yG2y8ukq3uIC";
int time1 = 0; //Biến đếm số lần thông báo hết đồ ăn về thiết bị
int time2 = 0; //Biến đếm số lần thông báo hết nước uống về thiết bị

WiFiClient wifiClient2; //Wificlient kết nối với Thingspeak
WiFiClient wifiClient; //Wificlient kết nối với Node-red
PubSubClient mqttClient(wifiClient); 

//Hàm kết nối wifi
void wifiConnect() {
  delay(10);
  // starting by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.mode(WIFI_STA);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" Connected!");
}


void mqttConnect() {
  while(!mqttClient.connected()) {
    Serial.println("Attemping MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if(mqttClient.connect(clientId.c_str())) {
      Serial.println("connected");
      mqttClient.subscribe("psc/power"); //Kết nối với kênh nhận tín hiệu phun sương
    }
    else {
      Serial.println("try again in 5 seconds");
      delay(5000);
    }
  }
}

//MQTT Receiver
void callback(char* topic, byte* message, unsigned int length) {
  Serial.println(topic);
  String strMsg;
  for(int i=0; i<length; i++) {
    strMsg += (char)message[i]; //Lấy dữ liệu truyền về vào biến message 
  }
  Serial.println(strMsg);
  if(strMsg == "On") //Mở phun sương (tương tác từ xa)
    Misting = 1;
  else //Tắt phun sương (tương tác từ xa)
    Misting = 0;
}


//Hàm gửi thông báo hết thức ăn về thiết bị di động người dùng
void sendFoodRequest() {
  WiFiClient client;
  while(!client.connect(host, httpport)) {
    Serial.println("connection fail");
  }

  client.print(String("GET ") + food_request + " HTTP/1.1\r\n"
              + "Host: " + host + "\r\n"
              + "Connection: close\r\n\r\n");

  while(client.available()) {
    String line = client.readStringUntil('\r');
  }
}

//Hàm gửi thông báo hết nước uống về thiết bị di động người dùng
void sendWaterRequest() {
  WiFiClient client;
  while(!client.connect(host, httpport)) {
    Serial.println("connection fail");

  }

  client.print(String("GET ") + water_request + " HTTP/1.1\r\n"
              + "Host: " + host + "\r\n"
              + "Connection: close\r\n\r\n");

  while(client.available()) {
    String line = client.readStringUntil('\r');
  }

}

void setup() {
  lcd.init();                     
  lcd.createChar(3, degreeSymbol);
  lcd.begin(20, 4);

  //Cài đặt servo nước uống
  servo_water.attach(servo_water_Pin); 
  servo_water.write(0); //Giá trị góc đầu tiên của servo = 0 độ
  //Cài đặt servo thức ăn
  servo_food.attach(servo_food_Pin); 
  servo_food.write(0); //Giá trị góc đầu của servo = 0 độ

  Serial.begin(9600);

  //Kết nối WiFi
  Serial.println("Connecting to WiFi");
  wifiConnect();

  //Mqtt(mosquitto) - kết nối trung gian
  mqttClient.setServer(mqttServer, port);
  mqttClient.setCallback(callback);

  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  
  //Cài đặt các cảm biến trọng lượng
  food.begin(23, 2); //Cảm biến trọng lượng khay thức ăn
  water.begin(4,19); //Cảm biến trọng lượng khay nước uống 
  stored_food.begin(35, 27); //Cảm biến trọng lượng hộp trữ thức ăn
  stored_water.begin(26,25); //Cảm biến trọng lượng hộp trữ nước uống 

  //Cài đặt cảm biến nhiệt độ - độ ẩm
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);

  //Cài đặt nút bấm khởi động máy
  pinMode(32, INPUT);

  //Cài đặt đèn báo máy đang hoạt động (đèn xanh lá)
  pinMode(18, OUTPUT);

  //Cài đặt đèn báo hết thức ăn / nước uống dự trữ (đèn đỏ)
  pinMode(5, OUTPUT);

  //Cài đặt đèn báo đang phun sương (đèn xanh dương)
  pinMode(14, OUTPUT);

  //Cài đặt kết nối ThingSpeak 
  ThingSpeak.begin(wifiClient2);
}


void loop() {
  //Khai báo các biến lưu thông tin truyền đi  
  char temperature[50], humidity[50], buffer_food[50], buffer_water[50];

  //Kiểm tra nếu nút bấm thay đổi trạng thái
  int button_state = digitalRead(32);
  if(button_state != lastState){
    lastMillis = millis();
    lastState = button_state;
  }


  if(button_state == HIGH){
    //Nếu nút được nhấn giữ trong khoảng 3s và trạng thái máy đang là tắt -> mở máy 
    if(millis() - lastMillis > 1000 && Machine_status == 0){
      Machine_status = 1; //Đổi giá trị của biến thành 1 -> máy hoạt động
      lastMillis = millis(); //Cài đặt lại giá trị thời gian gần nhất 
      Serial.println("Machine On!");
      digitalWrite(18, HIGH); // Mở đèn xanh báo hiệu máy đang hoạt động
      Serial.println("Machine starting up...");
      delay(5000);
      lcd.backlight();               

    }

    //Nếu nút được nhấn và trạng thái máy đang là bật -> tắt máy
    else if(Machine_status == 1){
      Machine_status = 0; //Đổi giá trị biến thành 0 -> máy ngừng hoạt động
      lastMillis = millis(); //Cài đặt lại giá trị thời gian gần nhất 
      Serial.println("Machine Off!");
      digitalWrite(18, LOW); //Tắt đèn xanh báo hiệu máy đã ngừng hoạt động
      digitalWrite(5, LOW); //Tắt đèn đỏ (nếu đang bật)
      digitalWrite(14, LOW); //Tắt đèn xanh dương (nếu đang bật)
      time1 = 0; //Cài đặt lại số lần tối đa thông báo thức ăn về 0
      time2 = 0; //Cài đặt lại số lần tối đa thông báo nước uống về 0

      //Xóa hết các dòng và tắt đèn màn hình LED
      lcd.setCursor(0, 0);           
      lcd.print("                    ");
      lcd.setCursor(0, 1);           
      lcd.print("                    ");
      lcd.setCursor(0, 2);           
      lcd.print("                    ");
      lcd.setCursor(0, 3);           
      lcd.print("                    ");
      lcd.noBacklight(); //Tắt màn hình LED
    }
  }

  //Nếu máy được xác định có hoạt động
  if(Machine_status == 1){
    //Lấy giá trị các cảm biến trọng lượng (trả về đơn vị gam đối với thức ăn và ml đối với nước uống)
    float stored_food_status = ceil(stored_food.get_units()/0.42); //Cảm biến ở hộp thức ăn dự trữ
    float stored_water_status = ceil(stored_water.get_units()/0.42); //Cảm biến ở hộp nước uống dự trữ
    float food_status = ceil(food.get_units()/0.42); //Cảm biến ở khay thức ăn
    float water_status = ceil(water.get_units()/0.42); //Cảm biến ở khay nước uống

    //Kết nối Node-red
    if(!mqttClient.connected()) {
      mqttConnect();
    }
    mqttClient.loop();

    //Kiểm tra khối lượng và mở/đóng servo để thả thức ăn
    if(food_status < 10){ //Nếu thức ăn trong khay ít hơn 10 gam 
      servo_food.write(90); //Mở cổng thả (quay servo góc 90 độ)
    }
    if(food_status > 150){ //Nếu thức ăn trong khay nhiều hơn 150 gam
      servo_food.write(0); //Đóng cổng thả (quay servo góc 0 độ)
    }

    //Kiểm tra và mở/đóng servo để thả nước uống
    if(water_status < 10){ //Nếu nước uống trong khay ít hơn 10 ml
      servo_water.write(90); //Mở cổng thả (quay servo góc 90 độ)
    }
    if(water_status > 300){ //Nếu nước uống trong khay nhiều hơn 300 ml
      servo_water.write(0); //Đóng cổng thả (quay servo góc 0 độ)
    }

    //Kiểm tra khối lượng thức ăn dự trữ
    if(stored_food_status <= 10){
      Serial.println("Out of food reserves!");
    }

    //Kiểm tra khối lượng nước uống dự trữ
    if(stored_water_status<= 10){
      Serial.println("Out of reserve water!");
    }


    //Lấy nhiệt độ và độ ẩm
    TempAndHumidity data = dhtSensor.getTempAndHumidity();
    float temp = data.temperature;
    float humid = data.humidity;

    //Nếu nhiệt độ quá 28 độ C hoặc độ ẩm dưới 35% hoặc người dùng đang gọi phun sương từ xa thì bật phun sương
    if(temp > 28 || humid < 35 || Misting == 1){
      Serial.println("Misting...");
      digitalWrite(14, HIGH); //Bật đèn xanh dương
    }
    else{
      digitalWrite(14, LOW); //Tắt đèn xanh dương
    }


    //Lưu dữ liệu nhiệt độ vào biến truyền tới Node-red
    sprintf(temperature, "%2.2f", temp);
    mqttClient.publish("psc/temp", temperature);

    //Lưu dữ liệu độ ẩm vào biến truyền tới Node-red
    sprintf(humidity, "%2.2f", humid);
    mqttClient.publish("psc/humidity", humidity);

    //In nhiệt độ ra màn hình LCD
    lcd.setCursor(0, 0);           
    lcd.print("Temperature: "); 
    lcd.print(temp); 
    lcd.print("\x03");
    lcd.print("C"); 

    //In độ ẩm ra màn hình LCD
    lcd.setCursor(0, 1);          
    lcd.print("Humidity: ");
    lcd.print(humid);
    lcd.print("% ");

    //Nếu hết cả nước uống và thức ăn dự trữ thì thông báo ra màn hình và bật đèn đỏ
    if(stored_water_status <= 10 && stored_food_status <= 10){
      Serial.println("Out of both!");
      digitalWrite(5, HIGH);
    }


    //Kiểm tra khối lượng thức ăn dự trữ
    lcd.setCursor(0, 2);   
    lcd.print("Food: ");         
    if(stored_food_status > 10){//Nếu khối lượng thức ăn dự trữ lớn hơn 10g
      lcd.print(stored_food_status); //In khối lượng thức ăn dự trữ ra màn hình LCD
      lcd.print(" g     "); 
      digitalWrite(5, LOW); //Tắt đèn báo hiệu (đèn đỏ)
      sprintf(buffer_food, "%f", stored_food_status); //Lưu khối lượng thức ăn dự trữ vào biến truyền dữ liệu tới Node-red
    }
    else{//Nếu khối lượng thức ăn dự trữ dưới 10g
      digitalWrite(5, HIGH); //Bật đèn báo hiệu (đèn đỏ)
      lcd.print("Out of food"); //In thông báo hết thức ăn dự trữ
      sprintf(buffer_food, "Out of food!"); //Lưu thông báo hết thức ăn dự trữ vào biến truyền dữ liệu tới Node-red
      if (time1 < 5){ //Số lần thông báo dưới 5 lần
        sendFoodRequest(); //Kích hoạt thông báo hết thức ăn về điện thoại người dùng (IFTTT)
        time1 = time1 + 1; //Biến đếm số lần thông báo
      }
    }

    //Kiểm tra khối lượng nước 
    lcd.setCursor(0, 3);          
    lcd.print("Water: ");
    if(stored_water_status > 10){//Nếu khối lượng nước uống dự trữ lớn hơn 10g
      lcd.print(stored_water_status); //In khối lượng nước uống dự trữ ra màn hình LCD
      lcd.print(" ml   "); 
      digitalWrite(5, LOW); //Tắt đèn báo hiệu (đèn đỏ)
      sprintf(buffer_water, "%f", stored_water_status); //Lưu khối lượng nước uống dự trữ vào biến truyền dữ liệu tới Node-red
    }
    else{//Nếu khối lượng nước uống dự trữ dưới 10g
      lcd.print("Out of water");  //In thông báo hết nước uống dự trữ
      digitalWrite(5, HIGH);  //Bật đèn báo hiệu (đèn đỏ)
      sprintf(buffer_water, "Out of water!"); //Lưu thông báo hết nước uống dự trữ vào biến truyền dữ liệu tới Node-red
      if (time2 < 5){ //Số lần thông báo dưới 5 lần
        sendWaterRequest(); //Kích hoạt thông báo hết thức ăn về điện thoại người dùng (IFTTT)
        time2 = time2 + 1; //Biến đếm số lần thông báo
      }
    }
    
    mqttClient.publish("psc/food-status", buffer_food); //Truyền dữ liệu thức ăn lên Node-red
    mqttClient.publish("psc/water-status", buffer_water); //Truyền dữ liệu nước uống lên Node-red


    //Cài đặt dữ liệu vào biến truyền dữ liệu ThingSpeak
    ThingSpeak.setField(1,temp); //Truyền nhiệt độ
    ThingSpeak.setField(2,humid); //Truyền độ ẩm
    ThingSpeak.setField(3,food_status); //Truyền thức ăn trong khay
    ThingSpeak.setField(4,water_status); //Truyền nước uống trong khay

    int x = ThingSpeak.writeFields(myChannelNumber,myWriteAPIKey); //Cập nhật dữ liệu lên ThingSpeak (Trung bình 17s/lần)
    if(x == 200){
      Serial.println("Data updated!");
    }
  }

}

