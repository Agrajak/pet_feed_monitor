/*
  SUHYUN, JEON - 전수현(12151616), korbots@gmail.com
*/

#define COM_UNKNOWN 0
#define COM_HELLO 1
#define COM_INIT_CLOCK 2
#define COM_SET_CLOCK 3
#define COM_CURRENT_CLOCK 4
#define COM_HELP 5
#define COM_GET_WEIGHT 6
#define COM_SET_ZERO 7
#define COM_SAVE 8
#define COM_RESET 9
#define COM_SAVE_WEIGHT 10
#define COM_GET_DIFF 11
#define COM_SHOW_LAST_1MIN 12
#define COM_SHOW_LAST_1HOUR 13
#define COM_SHOW_LAST_1DAY 14
#define COM_SHOW_ALL 15

#define COMMAND 0 
#define CURRENT_TIME 1

#define CHECK_SCALE_PER_MINUTE 1
#define ADDR_DS1307 0b1101000
#define ADDR_24LC02B 0b1010000
#define SAMPLE_N 10
#define DEBUG 1
#define BLUETOOTH_LISTEN 1
#define VERBOSE 1

#define SCALE_FACTOR 38.7

uint8_t days[14]={0,31,28,31,30,31,30,31,31,30,31,30,31};

volatile uint8_t data;
volatile uint8_t waitingFor;
volatile char rBuf[30];
volatile char wBuf[100];
volatile uint8_t rcnt, wcnt;
volatile uint8_t SLAVE_ADDR;
volatile int cnt;
volatile int cnt2=0;

// this is for weight
volatile uint32_t beforeWeight=0;
volatile uint32_t afterWeight=0;
volatile long int offset;
volatile uint8_t data_length=0; 

// boolean flags, can be optimzied by set/clear uint8_t.
volatile uint8_t isChecking = 0, flag2 = 0, flag3 = 0;
volatile uint8_t startFeeding = 0, finishFeeding = 0;
volatile uint8_t saveTimeFlag = 0;
volatile uint8_t isTimerDone = 0;
volatile uint8_t hasError = 0;
volatile uint8_t isWeightInvalid = 0;
//////////////////////////////// -- INIT REGISTERS --- ///////////////////////////////////

// following interrupt vector names referenced from http://ee-classes.usc.edu/ee459/library/documents/avr_intr_vectors/
void initLEDs(){
  DDRD |= (1 << DDD4) | (1 << DDD7); // LED3
  DDRB |= (1 << DDB1) | (1 << DDB2); // LED1 and LED2
  // PD3 -> INT1
  // PD4 -> SCK for HX711
}
void LED1(uint8_t a){ 
  PORTB = (PORTB & ~(1<<PORTB1)) | (a<<PORTB1);
}
void LED2(uint8_t a){
  PORTB = (PORTB & ~(1<<PORTB2)) | (a<<PORTB2); 
}
void LED3(uint8_t a){
  PORTD = (PORTD & ~(1<<PORTD4)) | (a<<PORTD4);
} 
void togglePulse(uint8_t a){
  PORTD = (PORTD & ~(1<<PORTD7)) | (a<<PORTD7);
}
void initDelay(){
  // External Interrupt Guide on p.79
  // AVR Status Register p.20
  // External Interrupt Mask Register
  // SREG |= 1 << 7;

  EIMSK = (1 << INT0) | (0 << INT1); // External Interrupt 0 is enabled.
  // External Interrupt Control Register A
  EICRA = (1 << ISC01) | (1 << ISC00) | (0 << ISC11) | (0 << ISC10); // rising edge of INT0 generate an interrupt request. 

  // 8bit Timer/Counter2 with PWM and Asynchoronous Operation p.155
  // Timer/Counter2 Control Register A - controls the Output Compare pin (OC2A) behavior.

  TCCR2A = 0; // OC0A, OC0B disabled, Wave Form Generator : Normal Mode!
  // Update of OCRx at Immediate, TOV flag set on MAX. (p.164)

  // Asynchoronous Status Register (p.167)
  ASSR = (1<< AS2 | 1<<EXCLK);

  // Timer/Counter2 Control Register B - CS22:0, (Clock Select)  
  //  TCCR2B = 0b110 << CS20; // ClkT2S / 1024

  // we can stop time/counter2 clock by set TCCR2B = 0;
  
  // Timer/Counter2 Interrupt Mask Register
  TIMSK2 = 1 << TOIE2; // Timer/Counter2 Overflow Interrupt Enable

  // 8bit Timer/Counter1 with PWM and Asynchoronous Operation p.140
  TCCR1A = 0;
  TIMSK1 = 1 << TOIE1;
  TCCR1B = 0b101 << CS10; // p.143

  SREG |= 1 << 7; // sei();
}
void initUART(){
//  // USART INIT (Manual 179Page)
  UBRR0H = (uint8_t) (103>>8);
  UBRR0L = (uint8_t) (103);
    
  // Fosc = 16.0Mhz, BaudRate = 9600bps, UBRRn => 103(U2Xn=0)
  // U2Xn=0 -> Normal mode, U2Xn=1-> Double Speed mode!
  // UCSR0A : (page 201)
  
  // UCSR0B = (1<<RXCIE0) | (1<<TXCIE0) | (1<<RXEN0) | (1<<TXEN0);

  // <삽질노트> - 쓰지도 않는 Interrupt를 켜놓고 SREG = 1<<I를 해서
  // 글로벌 인터럽트를 Enable 시켜버리니까
  // Interrupt를 처리하다가 Stack Overflow로 main 함수가 무한 반복됨.
  // => 사용하지도 않는 RXCI, TXCI 끄기

  // Master, Reciever Enable!
  UCSR0B = (1 << RXEN0) | (1 << TXEN0);
  // Enabling Interrupts

  // UCSR0C = (1<<UCSZ01) | (1<<UCSZ00);
  // for HC-05, Data bit: 8bit, Stop bit: 1bit, no parity bit!
  // UCSR0C의 경우 기본 값이 Async USART, Parity Disabled, 1 stop-bit, 8 databit 이라서 설정해줄 필요가 없음!

  // Transmit and Recievie Examples on Manual 186page
}
void initTWI(){ // p.225
  // SCL freq =  16000khz / 16 + 2(TWBR)(PresclaeValue)
  // 16 + 2TWBR = 160
  // Target : SCL : 100Khz
  // TWBR = 72
  TWBR = 72;
  TWSR = 0;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////// -- PROTOTYPES -- ///////////////////////////

//     ## COMMUNICATION ##

// - UART
void respond(char *); // 출력

// - TWI(I2C)
void setSlaveAddr(uint8_t);
void senSLARW(uint8_t);
void startTWI();
void stopTWI();
void clearTWCR();
uint8_t readTWI(uint16_t);
uint8_t writeTWI(uint16_t, uint8_t);

// - TWI(EEPROM)
void loadClock(); // using EEPROM
void loadDataLength();
void resetEEPROM();
// - TWI(RTC Module, DS1307)
void setCurrentTime(char *);
void initClock();

uint8_t getSecond(uint8_t);
uint8_t getMinute(uint8_t);
uint8_t getHour(uint8_t);
uint8_t getDate(uint8_t);
uint8_t getMonth(uint8_t);
uint8_t getYear(uint8_t);
void getCurrentTime(); // EEPROM -> RTC Module
void saveCurrentTime(); // RTC Module -> EEPROM every minutes

// - 8bit Time/Counter2
void startTimer(uint8_t);
void stopTimer();
void delay_ms(int);
void delay_us(int);

// - HX711 (24bit ADC)
uint32_t readWeight();
uint32_t getWeight();
void setZero();
void togglePulse(uint8_t);

long int getDiff(); // 얼마만큼 사료가 소비되었는가 측정
void clearDiff(); // getDiff를 초기화시키고 EEPROM에 기록하는가 검사

//     ## Miscellaneous ##
void debugValue(uint8_t);
int verifyCommand();
void showHelp();
void checkNextLine();
uint8_t charToInt(char *, uint8_t, uint8_t);
void showDatas(long int time);
long int dateToTimeStamp(uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);
uint8_t decimalTo8bit(uint8_t);

///////////////////////////////////////////////////////////////////////////////////////////////////////////

uint8_t readBit(int addr, uint8_t loc){
  return (addr & (1<<loc)) >> loc;
}

void debugValue(uint8_t v){
  if(DEBUG){
    LED1(v & 1);
    LED2((v & (1<<1)) >> 1);
    LED3((v & (1<<2)) >> 2);
  }
}

void setSlaveAddr(uint8_t addr){
  SLAVE_ADDR = addr;
}
void sendSLARW(uint8_t mode){
  TWDR = (SLAVE_ADDR << 1) + mode;
  TWCR = (1<<TWINT) | (1<<TWEN);
  while (!(TWCR & (1<<TWINT)));
}

void startTWI(){
  TWCR = (1<<TWINT) | (1<<TWSTA) | (1<<TWEN); 
  while(!(TWCR & (1<<TWINT)));
}
void stopTWI(){
  TWCR = (1<<TWINT) | (1<<TWSTO) | (1<<TWEN); 
  while(!(TWCR & (1<<TWSTO)));

  delay_ms(100);
}
void clearTWCR(){
  TWCR = (1<<TWINT) | (1<<TWEN);
  while (!(TWCR & (1<<TWINT)));
}
uint8_t readTWI(uint16_t addr){
  // Master Reciever TWSR 코드 정리는 230페이지에 있음!

  uint8_t data;
  startTWI();  // ====> START
  if((TWSR & 0xF8) != 0x08) { respondByte(TWSR & 0xF8); return (TWSR & 0xF8); }
  
  sendSLARW(0);   // ====> SLA+W
  if((TWSR & 0xF8) != 0x18) { respondByte(TWSR & 0xF8); stopTWI(); return (TWSR & 0xF8); }
  
  TWDR = addr; 
  clearTWCR(); // ====> WORD ADDRESS
  if((TWSR & 0xF8) != 0x28) { respondByte(TWSR & 0xF8); stopTWI();  return (TWSR & 0xF8); }

  startTWI();  // ====> RE-START
  if((TWSR & 0xF8) != 0x10) { respondByte(TWSR & 0xF8); stopTWI();  return (TWSR & 0xF8); }

  sendSLARW(1);   // ====> SLA+R
  if((TWSR & 0xF8) != 0x40) { respondByte(TWSR & 0xF8); stopTWI();  return (TWSR & 0xF8); }
  clearTWCR(); 
  data = TWDR; 

  clearTWCR(); // ====> VALUE
  
  // 마지막으로 ACK가 올 수도 있고, N-ACK가 올 수도 있다.
  if((TWSR & 0xF8) != 0x50 && (TWSR & 0xF8) != 0x58) { respondByte(6); stopTWI();  return (TWSR & 0xF8); }

  stopTWI(); // ====> STOP
  
  return data;
}
uint8_t writeTWI(uint16_t addr, uint8_t data){
  // Master Trasmitter TWSR 정리는 227페이지에 있음!
  startTWI();  // ====> START
  if((TWSR & 0xF8) != 0x08) { respondByte(TWSR & 0xF8); return (TWSR & 0xF8); }

  sendSLARW(0);   // ====> SLA+W
  if((TWSR & 0xF8) != 0x18) { respondByte(TWSR & 0xF8); stopTWI();  return (TWSR & 0xF8); }
  
  TWDR = addr; 
  clearTWCR(); // ====> WORD ADDRESS
  if((TWSR & 0xF8) != 0x28) { respondByte(TWSR & 0xF8); stopTWI();  return (TWSR & 0xF8); }

  TWDR = data; 
  clearTWCR(); // ====> WORD ADDRESS
  if((TWSR & 0xF8) != 0x28) { respondByte(TWSR & 0xF8); stopTWI();  return (TWSR & 0xF8); }

  stopTWI();   // ====> STOP
}
int verifyCommand(){
  // TODO: checkout rcnt length!
  if(rBuf[0]=='H'){
    if(rBuf[1] == 'I' && rcnt == 2) return COM_HELLO;
    else if(rBuf[1] =='E' && rBuf[2] == 'L' && rBuf[3] == 'P' && rcnt == 4){
      return COM_HELP;
    }
  }    
  else if(rBuf[0]=='C'){
    if(rBuf[1] == 'W' && rcnt == 2)
      return COM_SAVE_WEIGHT;
    if(rBuf[1] == 'T' && rcnt == 2)
      return COM_CURRENT_CLOCK;
  }
  else if(rBuf[0]=='I' && rBuf[1]=='T' && rcnt == 2){
    return COM_INIT_CLOCK;
  }
  else if(rBuf[0]=='S'){
    if(rBuf[1] == 'T' && rcnt == 2){
      return COM_SET_CLOCK;
    }
    else if(rBuf[1] == 'A' && rBuf[2] == 'V' && rBuf[3] == 'E' && rcnt == 4){
      return COM_SAVE;
    }
    else if(rBuf[1] == '_'){
      if(rBuf[2] == 'A' && rBuf[3] == 'L' && rBuf[4] =='L'){
        return COM_SHOW_ALL;
      }
      if(rBuf[2] == '1'){
        if(rBuf[3] == 'M' && rBuf[4] == 'I' && rBuf[5] =='N'){
          return COM_SHOW_LAST_1MIN;
        }
        if(rBuf[3] == 'H' && rBuf[4] == 'O' && rBuf[5] =='U' && rBuf[6] =='R'){
          return COM_SHOW_LAST_1HOUR;
        }
        if(rBuf[3] == 'D' && rBuf[4] == 'A' && rBuf[5] =='Y'){
          return COM_SHOW_LAST_1DAY;
        }
      }
    }
  }
  else if(rBuf[0] == 'X' && rcnt == 1){
    return COM_GET_WEIGHT;
  }
  else if(rBuf[0] == 'R'){
    if(rBuf[1] == 'E' && rBuf[2] == 'S' && rBuf[3] == 'E' && rBuf[4] == 'T' && rcnt == 5){
      return COM_RESET;
    }
  }
  else if(rBuf[0] == 'Z' && rBuf[1] == 'E' && rBuf[2] == 'R' && rBuf[3] == 'O' && rcnt == 4){
    return COM_SET_ZERO;
  }
  else if(rBuf[0] == 'G' && rBuf[1] == 'D' && rcnt == 2){
    return COM_GET_DIFF;
  }
  return COM_UNKNOWN;
}
void respond(char *buf){
  uint8_t i;
  for(i=0;;i++){
    if(buf[i] == 0) break;
    while(!(UCSR0A & (1<<UDRE0)));
    UDR0 = buf[i];    
  }  
  while(!(UCSR0A & (1<<UDRE0)));
  UDR0 = '\r';    
  while(!(UCSR0A & (1<<UDRE0)));
  UDR0 = '\n';    
}
void respondByte(uint8_t value){
  sprintf(wBuf, "[%x]", value);
  respond(wBuf);
}
void checkNextLine(){
  uint8_t c;
  uint32_t v;
  // if next line detected
  if(rcnt>= 2 && rBuf[rcnt-2] == '\r' && rBuf[rcnt-1] == '\n'){ // if \n\r is recieved!
    rcnt-=2;
    rBuf[rcnt] = 0;
    c = verifyCommand();

    if(waitingFor == COMMAND){
      if(c==COM_HELLO){
        LED3(1);
        respond("Nice to meet you!");
        LED3(0);
      }
      if(c==COM_CURRENT_CLOCK){
        LED3(1);
        getCurrentTime();
        LED3(0);
      }
      if(c==COM_SET_CLOCK){
        respond("Type Current Time in following format:");
        respond("YYYY:MM:DD-hh:mm");          
        waitingFor = CURRENT_TIME;
      }
      if(c==COM_INIT_CLOCK){
        LED3(1);
        initClock();
        LED3(0);
      }
      if(c==COM_GET_WEIGHT){
        LED3(1);
        v = getWeight();
        if(!isWeightInvalid){
          sprintf(wBuf, "weight is %ld.%d", v/10, v%10);
          respond(wBuf);
        }
        else {
          respond("You got invalid weight value, you should re-zero scale");
        }
        LED3(0);
      }
      if(c==COM_SET_ZERO){
        LED3(1);
        setZero();
        LED3(0);
      }
      if(c==COM_SAVE_WEIGHT){
        LED3(1);
        clearDiff();
        LED3(0);
      }
      if(c==COM_HELP){
        showHelp();
      }
      if(c==COM_GET_DIFF){
        LED3(1);
        long int diff = getDiff();
        sprintf(wBuf, "difference is %ld.%dg", diff/10, diff%10);
        respond(wBuf);
        LED3(0);
      }
      if(c==COM_SAVE){
        LED3(1);
        saveCurrentTime();
        LED3(0);
      }
      if(c==COM_SHOW_ALL){
        LED3(1);
        showDatas(0);
        LED3(0);
      }
      if(c==COM_SHOW_LAST_1MIN){
        LED3(1);
        showDatas(60);
        LED3(0);
      }
      if(c==COM_SHOW_LAST_1HOUR){
        LED3(1);
        showDatas(60*60);
        LED3(0);
      }
      if(c==COM_SHOW_LAST_1DAY){
        LED3(1);
        showDatas(60*60*24);
        LED3(0);
      }
      if(c==COM_RESET){
        LED3(1);
        resetEEPROM();
        LED3(0);
      }
      if(c==COM_UNKNOWN){
        respond("Undeclared Command!");
        respond("Type HELP to see commands.");
      }
    }
    else if(waitingFor == CURRENT_TIME){
        if(rcnt != 16) respond("Invalid Format!");
        else if(rBuf[0] != '2' || rBuf[1] != '0') respond("YYYY should be 20xx!");
        else {
          setCurrentTime(rBuf);
        }
        waitingFor = COMMAND;
    }
    rcnt = 0;
  }
}
void initClock(){
  respond("Start Init Clock!");
  setSlaveAddr(ADDR_DS1307);
  writeTWI(0x00, 0x00);
  writeTWI(0x01, 0x00);
  writeTWI(0x02, 0x00);
  writeTWI(0x04, 0x00);
  writeTWI(0x05, 0x00);
  writeTWI(0x06, 0x00);
  respond("Init Clock Done!");
}

void setCurrentTime(char *buf){
  uint8_t s, m, h, D, M, Y, d, ct;
  if(buf[4] == buf[7] && buf[7] == buf[13] && buf[13] == ':' && buf[10] == '-'){
    hasError = false;
    Y = charToInt(buf, 2, 4);
    M = charToInt(buf, 5, 7);
    D = charToInt(buf, 8, 10);
    h = charToInt(buf, 11, 13);
    m = charToInt(buf, 14, 16);
    s = 0;
    if(VERBOSE){
      sprintf(wBuf, "Y=20%d, M=%d, D=%d h=%d, m=%d, s=%d", Y, M, D, h, m, s);
      respond(wBuf);
      setSlaveAddr(ADDR_DS1307);
      writeTWI(0x00, ((s/10)<<4) | (s%10));
      writeTWI(0x01, ((m/10)<<4) | (m%10));
      writeTWI(0x02, ((h/10)<<4) | (h%10));
      writeTWI(0x04, ((D/10)<<4) | (D%10));
      writeTWI(0x05, ((M/10)<<4) | (M%10));
      writeTWI(0x06, ((Y/10)<<4) | (Y%10));
      saveCurrentTime();
    }
    if(hasError)
      respond("숫자로만 구성되어야합니다.");
    return;
  }
  respond("Invalid Format");
}
void getCurrentTime(){
  uint8_t s, m, h, D, M, Y, d, ct;
  setSlaveAddr(ADDR_DS1307);
  s = getSecond(readTWI(0x00));
  m = getMinute(readTWI(0x01));
  h = getHour(readTWI(0x02));
  D = getDate(readTWI(0x04));
  M = getMonth(readTWI(0x05));
  Y = getYear(readTWI(0x06));

  sprintf(wBuf, "Current Time : 20%d:%d:%d-%d:%d:%d", Y, M, D, h, m, s);
  respond(wBuf);
}
// DS1307 -> EEPROM
void saveCurrentTime(){
  uint8_t s, m, h, D, M, Y;
  setSlaveAddr(ADDR_DS1307);
  s = readTWI(0x00);
  m = readTWI(0x01);
  h = readTWI(0x02);
  D = readTWI(0x04);
  M = readTWI(0x05);
  Y = readTWI(0x06);

  setSlaveAddr(ADDR_24LC02B);
  writeTWI(0x004, s);
  writeTWI(0x005, m);
  writeTWI(0x006, h);
  writeTWI(0x007, D);
  writeTWI(0x008, M);
  writeTWI(0x009, Y);
//  respond("Save is Done!");
}
// EEPROM -> DS1307
void loadCurrentTime(){
  uint8_t s, m, h, D, M, Y;
  setSlaveAddr(ADDR_24LC02B);
  s = readTWI(0x004);
  m = readTWI(0x005);
  h = readTWI(0x006);
  D = readTWI(0x007);
  M = readTWI(0x008);
  Y = readTWI(0x009);
  
  setSlaveAddr(ADDR_DS1307);
  writeTWI(0x00, s);
  writeTWI(0x01, m);
  writeTWI(0x02, h);
  writeTWI(0x04, D);
  writeTWI(0x05, M);
  writeTWI(0x06, Y);
}
int main(){
  // init registers
  initLEDs();
  initUART();
  initTWI();
  initDelay(); 
  uint8_t Y, M, D, h, m, s;   
  respond("--------------------------------------------");

  // load stuff
  loadCurrentTime();
  loadZero();
  loadDataLength();

  s = getSecond(readTWI(0x004));
  m = getMinute(readTWI(0x005));
  h = getHour(readTWI(0x006));
  D = getDate(readTWI(0x007));
  M = getMonth(readTWI(0x008));
  Y = getYear(readTWI(0x009));


  sprintf(wBuf, "현재 시간: 20%d:%d:%d-%d시%d분%d초", Y, M, D
  , h, m, s);
  respond(wBuf);

  Y = getYear(readTWI(0x00A));
  M = getMonth(readTWI(0x00B));
  D = getDate(readTWI(0x00C));
  h = getHour(readTWI(0x00D));
  m = getMinute(readTWI(0x00E));
  s = getSecond(readTWI(0x00F));

  sprintf(wBuf, "마지막에 사료량을 측정한 시간: 20%d:%d:%d-%d시%d분%d초", Y, M, D, h, m, s);

  respond(wBuf);

  // turn led1 on!
  respond("======================================");
  respond("  Welcome. type HELP to see commands!  ");
  respond("--------------------------------------------");
  while(1){
    if(BLUETOOTH_LISTEN){
    // wait until RX will be prepared!      
      while(!(UCSR0A & (1<<RXC0))){
        // 1분마다
        if(saveTimeFlag){
          LED3(1);
          saveCurrentTime();
          LED3(0);
          saveTimeFlag = 0;
        }
        // 버튼이 제일 처음 빨간색으로 변할 떄
        if(startFeeding == 1 && finishFeeding == 0){
          LED3(1);
          isChecking = 1;
          // 이미 beforeWeight를 기록 중이었다면 EEPROM에 저장
          if(beforeWeight != 0){
            clearDiff();
          }
          finishFeeding = 1; 
          isChecking = 0;
          LED2(1);
          LED3(0);
        }
        // 버튼이 꺼질 때
        else if(startFeeding == 0 && finishFeeding == 1){
          LED3(1);
          isChecking = 1;
          // beforeWeight 기록 초기화
          afterWeight = 0;
          beforeWeight = getWeight();

          startFeeding = finishFeeding = 0;
          isChecking = 0;
          LED3(0);
          LED2(0);
        }
        else if(flag3){ // 5분마다 ACTIVATED 된다.
          LED3(1);
          respond("<1분마다 정기적으로 하는 무게검사>");
          if(startFeeding == 0 && finishFeeding == 0)
            clearDiff();
          flag3 = 0;
          LED3(0);
        }
      }
      rBuf[rcnt++] = UDR0;
      checkNextLine(); 
    }
  }
}
void startTimer(uint8_t scale){
  TCCR2B = scale << CS20; //  p.165
  isTimerDone = 0;
  // 0b000 -> No clock source
  // 0b001 -> (No Prescaling)
  // 0b010 -> /8
  // 0b011 -> /32
  // 0b100 -> /64
  // 0b101 -> /128
  // 0b110 -> /256
  // 0b111 -> /1024
}
void stopTimer(){
  TCCR2B = 0; 
}
void delay_us(int time){
  // embedded C에서 int의 크기가 16비트임을 주의!
//  cnt2 = 64UL*time/1000UL;
  cnt2 = 1;
  startTimer(0b001);
  while(!isTimerDone); // ISR에서 isTimerDone 토글!
  stopTimer();
}
void delay_ms(int time){
  cnt2 = 64UL*time/1000UL;
  startTimer(0b111);
  while(!isTimerDone);
  stopTimer();
}
// ISR
ISR(INT0_vect){
  if(!isChecking){
    if(startFeeding == 0 && finishFeeding == 0){
      startFeeding = 1;
      finishFeeding = 0;
    }
    else if(startFeeding == 1 && finishFeeding == 1){
      startFeeding = 0;
      finishFeeding = 1;
    }
  }
}
// checking every minutes;
ISR(TIMER1_OVF_vect){
  cnt++;
  if(cnt % 15 == 0){ // 1분 마다
    saveTimeFlag = 1;
  }
  if(cnt == CHECK_SCALE_PER_MINUTE * 15){
    flag3 = 1;
    cnt = 0;
  }
}
// implementation for delay_ms, and delay_us;
ISR(TIMER2_OVF_vect){
  if(cnt2 != 0){
    cnt2--;
    if(cnt2 == 0) isTimerDone = 1;
  }
}
uint8_t charToInt(char *buf, uint8_t _start, uint8_t _end){
  uint8_t v = 0, d = 1, i;
  for(i=_end-1;i>=_start;i--){
    // Digit Check!
    if(buf[i] >= '0' & buf[i] <='9'){
      v += (buf[i]-'0')*d;
      d *= 10;    
    }
    else {
      hasError = true;
      break;
    }
  }   
  return v;
}

uint32_t getWeight(){
  isWeightInvalid = false;
  uint32_t v = readWeight();
  if((long)v + offset > 0){
    return (v+offset)/SCALE_FACTOR;
  }
  else if((long)v + offset > -3*SCALE_FACTOR){ // -0.3g 까지는 보정해주자.
    return 0;
  }
//  sprintf(wBuf, "v is %ld(%ld), offset is %ld(%ld), v+offset is %ld", v, (long)v, offset, (long)offset, (long)v+offset);
//  respond(wBuf);
  respond("Invalid value detected. \n Re-zero should be done!");
  isWeightInvalid = true;
  return 0;
}
uint32_t readWeight(){
  uint8_t i, s; 
  uint32_t v=0;
  uint8_t highPeriod = 40, data[3]={0}, pos=7;
  long _min=0, _max=0, _sum=0;
  // HX711 datasheet p.5

  // Pulse 25번 -> GAIN 128
  // Pulse 27번 -> GAIN 64
  // MSB --------------------- LSB
  for(s=0;s<SAMPLE_N;s++){
    data[2] = data[1] = data[0] = 0;
    delay_ms(100);
    for(i=0;i<8;i++){
      togglePulse(1); delay_us(10);
      data[2] += ((PIND & (1<<PORTD3)) >> PORTD3) << (7-i);
      delay_us(10); togglePulse(0); delay_us(10); delay_us(10);
    }

    for(i=0;i<8;i++){
      togglePulse(1); delay_us(10);
      data[1] += ((PIND & (1<<PORTD3)) >> PORTD3) << (7-i);
      delay_us(10); togglePulse(0); delay_us(10); delay_us(10);
    }

    for(i=0;i<8;i++){
      togglePulse(1); delay_us(10);
      data[0] += ((PIND & (1<<PORTD3)) >> PORTD3) << (7-i);
      delay_us(10); togglePulse(0); delay_us(10); delay_us(10);
    }

    for(i=0;i<1;i++){
      togglePulse(1); delay_us(10); delay_us(10); 
      togglePulse(0); delay_us(10); delay_us(10);
    }
    v = ( (((uint32_t)((data[2] & 0x80) ? 0xFF : 0x00)) << 24)
        | (((uint32_t)(data[2])) << 16)
        | (((uint32_t)(data[1])) << 8)
        | (((uint32_t)(data[0])) ));
    _sum += v;
    if(s==0){
      _min = v;
      _max = v;
    }
    else {
      if(_min > v) _min = v;
      else if(_max < v) _max = v;
    }

  }
  _sum -= (_min + _max);
  _sum /= (SAMPLE_N - 2);
  return _sum;
}
uint8_t getSecond(uint8_t addr){
  return ((addr & 0x70)>>4) * 10 + (addr & 0x0F);
}
uint8_t getMinute(uint8_t addr){
  return ((addr & 0x70)>>4) * 10 + (addr & 0x0F);
}
uint8_t getHour(uint8_t addr){
  return ((addr & 0x10)>>4) * 10 + (addr & 0x0F);
}
uint8_t getDate(uint8_t addr){
  return ((addr & 0x30)>>4) * 10 + (addr & 0x0F);
}
uint8_t getMonth(uint8_t addr){
  return ((addr & 0x10)>>4) * 10 + (addr & 0x0F);
}
uint8_t getYear(uint8_t addr){
  return ((addr & 0xF0)>>4) * 10 + (addr & 0x0F);
}
uint8_t decimalTo8bit(uint8_t v){
  return ((v/10)<<4) | (v%10);
}

void setZero(){
  offset = readWeight();
  offset = -offset;

  uint8_t data[3] = {0};
  // SLAVE ON!
  if(VERBOSE){
    sprintf(wBuf, "offset is %ld, %x", offset, offset);
    respond(wBuf);
  }
  setSlaveAddr(ADDR_24LC02B);

  data[2] = (offset & 0xFF0000) >> 16;
  data[1] = (offset & 0x00FF00) >> 8;
  data[0] = (offset & 0x0000FF);
  
  writeTWI(0x002, data[2]);
  writeTWI(0x001, data[1]);
  writeTWI(0x000, data[0]);
  
  if(VERBOSE) respond("write to EEPROM Complete!");
}
void loadZero(){
  uint8_t data[3] = {0};
  // load zero offset from EEPROM
  setSlaveAddr(ADDR_24LC02B);
  data[2] = readTWI(0x002);
  data[1] = readTWI(0x001);
  data[0] = readTWI(0x000);
  offset = (data[2] << 16) | (data[1] << 8) | (data[0]);
  if(VERBOSE){
    sprintf(wBuf, "Zeroing Offset loaded. (%ld)", offset);
    respond(wBuf);
  }
}
void showHelp(){
  /*
  respond("< 명령어들 >");
  respond("==== 일반 ====");
  respond("HI: say hello to system!");
  respond("CT: 현재 시스템 시간을 보여줍니다.");
  respond("IT: 시계를 0:0:0-0:0:0으로 초기화합니다. ");
  respond("ST: 시스템 시간을 수동으로 설정합니다.");
  respond("ZERO: 영점 조절을 합니다.");
  respond("X: 현재 무게 측정값을 출력합니다.");
  respond("RESET: EEPROM에 저장된 모든 정보를 초기화합니다.");
  respond("GD: 현재 사료 소비량을 계산합니다.");
  respond("CW: 현재 사료 소비량을 EEPROM에 저장합니다.");
  respond("==== 통계 ====");
  respond("S_1MIN: 최근 1시간동안의 사료 소비량을 출력합니다.");
  respond("S_1HOUR: 최근 6시간동안의 사료 소비량을 출력합니다.");
  respond("S_1DAY: 최근 하루동안의 사료소비량을 출력합니다.");
  respond("S_ALL: 저장되어있는 모든 소비 데이터를 출력합니다.");
  */
}
void clearDiff(){
  long int diff = getDiff();
  beforeWeight = getWeight();

  if(isWeightInvalid | diff > 10000){
    respond("You got invalid weight data. plz zero again");
  }
  else if(diff < 0){
    respond("FEED버튼을 누른상태로 보급을 해야합니다.");
  }
  else if(diff != 0){
    sprintf(wBuf, "%ld.%dg만큼 사료가 소비됨.", diff/10, diff%10);
    respond(wBuf);
    getCurrentTime();
    // [6bit] 0x009+n*8 ~ 0x00E(14)+n*8: n번째 정보의 날짜(Y-M-D:h-m-s)
    // [2bit] 0x00F(15)+n*8 ~ 0x010(16)+n*8: n번째 정보의 사료량 (최대 65536g)
    uint8_t s, m, h, D, M, Y, _d;
    uint16_t v = diff;

    setSlaveAddr(ADDR_DS1307);
    s = readTWI(0x00);
    m = readTWI(0x01);
    h = readTWI(0x02);
    D = readTWI(0x04);
    M = readTWI(0x05);
    Y = readTWI(0x06);

    setSlaveAddr(ADDR_24LC02B);
    writeTWI(0x009 + (data_length+1)*8, Y);
    writeTWI(0x00A + (data_length+1)*8, M);
    writeTWI(0x00B + (data_length+1)*8, D);
    writeTWI(0x00C + (data_length+1)*8, h);
    writeTWI(0x00D + (data_length+1)*8, m);
    writeTWI(0x00E + (data_length+1)*8, s);

    writeTWI(0x00F + (data_length+1)*8, (v & 0xFF00)>>8);
    writeTWI(0x010 + (data_length+1)*8, v & 0x00FF);

    writeTWI(0x00A, Y);
    writeTWI(0x00B, M);
    writeTWI(0x00C, D);
    writeTWI(0x00D, h);
    writeTWI(0x00E, m);
    writeTWI(0x00F, s);

    writeTWI(0x003, data_length+1);

    respond("EEPROM에 해당 정보 쓰기 완료!");
    data_length += 1;
    sprintf(wBuf, "현재 저장된 정보: %d개", data_length);
    respond(wBuf);
  }
  else {
    respond("무게 차이가 0.5g이내이므로 쓰지않음.");
  }
}
long int getDiff(){
  long int diff = 0;
  if(beforeWeight != 0){
    diff = beforeWeight - getWeight();
  }
  else {
    // 처음 체크하는 무게일때 이제 측정 시작을 알리는 LED 온!
    LED1(1);
  }
  if(-8 < diff && diff < 8){
    return 0;
  }
  return diff;
}
void loadDataLength(){
  setSlaveAddr(ADDR_24LC02B);
  data_length = 0;
  data_length = readTWI(0x003);
  if(VERBOSE){
    sprintf(wBuf, "%d개의 정보가 저장되어있음!", data_length);
    respond(wBuf);
  }
}
void resetEEPROM(){
  setSlaveAddr(ADDR_24LC02B);
  uint8_t i;
  for(i=0;i<0x00F;i++) writeTWI(i, 0);
  initClock();
  data_length = 0;
  offset = 0;
  respond("Reset EEPROM done!");
}

long int dateToTimeStamp(uint8_t Y, uint8_t M, uint8_t D, uint8_t h, uint8_t m , uint8_t s){
  long int v=0;
  v+=s; // 60초 => 1분
  v+=m*60; // 60분 => 1시간
  v+=h*60*60; // 24시간 => 1일
  v+=D*24*60*60; // M번째 달은 days[M]일 => 1달
  v+=M*24*60*60*days[M]; // 12달 => 1년
  v+=Y*24*60*60*days[M]*12;
  return v;
}

void showDatas(long int time){
  
  uint8_t i;
  uint8_t Y, M, D, h, m, s, v1, v2, n=0;
  uint16_t v=0, sum=0;
  long int now, t;
  
  setSlaveAddr(ADDR_DS1307);
  s = getSecond(readTWI(0x00));
  m = getMinute(readTWI(0x01));
  h = getHour(readTWI(0x02));
  D = getDate(readTWI(0x04));
  M = getMonth(readTWI(0x05));
  Y = getYear(readTWI(0x06));
  now = dateToTimeStamp(Y, M, D, h, m, s);
  setSlaveAddr(ADDR_24LC02B);
  for(i=1;i<=data_length;i++){
    Y = getYear(readTWI(0x009 + i*8));
    M = getMonth(readTWI(0x00A + i*8));
    D = getDate(readTWI(0x00B + i*8));
    h = getHour(readTWI(0x00C + i*8));
    m = getMinute(readTWI(0x00D + i*8));
    s = getSecond(readTWI(0x00E + i*8));
    
    t = dateToTimeStamp(Y, M, D, h, m, s);
    if(time != 0){
      if(now-t >= time){
        continue;
      }
    }

    n++;
    v1 = readTWI(0x00F + i*8) << 8;
    v2 = readTWI(0x010 + i*8);
    v = v1 | v2;
    sprintf(wBuf, "20%d년 %d월 %d일 %d시 %d분 %d초 -> %d.%dg 소비", Y, M, D
    , h, m, s, v/10, v%10);
    respond(wBuf);
    sum += v;
  }
  sprintf(wBuf, "총 %d개의 데이터가 존재합니다!", n);
  respond(wBuf);
  sprintf(wBuf, "총 사료 소비량: %d.%dg", sum/10, sum%10);
  respond(wBuf);
  
  
}
// ------------------------------------------------------ 
// EEPROM 배치도 2048 byte => 2^11
// ------------------------------------------------------ 
// [3bit] 0x000 ~ 0x002: offset 24bit for 영점조절

// [1bit] 0x003: 몇개의 정보가 저장되어있는가?
// [6bit] 0x004 ~ 0x009: 제일 최근에 저장된 날짜 (장비가 켜질때 해당 시간을 불러온다.)
// [6bit] 0x00A ~ 0x00F: 제일 마지막에 사료량을 기록한 날짜

// [6bit] 0x009+n*8 ~ 0x00E(14)+n*8: n번째 정보의 날짜(Y-M-D:h-m-s)
// [2bit] 0x00F(15)+n*8 ~ 0x010(16)+n*8: n번째 정보의 사료량 (최대 65536g)

// 캘리브레이션
// 100원 => 5.42g, 50원 => 4.16g

// 0.002g 단위
// 2mg단위로 측정가능!

// 0.002g * 2^24 => 33554

// 1949 => 5.42g  ==> 1g당 359.5
// 4163 => 10.84g  ==> 1g당 384g
// 5700 => 15g ==> 1g당 380

// 1g당 380