#include "ioCC2530.h"
#include <stdio.h>
#include <string.h>

#define LED1 P1_0
#define LED2 P1_1

char rf_rx_buf[128];    //rf缓冲区
char serial_rxbuf[128]; //串口接受缓冲区
int serial_rxpos = 0;
int serial_rxlen = 0;
char is_serial_receive = 0;

void uart0_init();
void uart0_sendbuf(char *pbuf, int len);
void uart0_flush_rxbuf();       //字面：接收数据流

void timer1_init();
void timer1_disable();
void timer1_enable();

void rf_send(char *pbuf, int len);
void rf_receive_isr();

void uart0_init()
{
  PERCFG = 0x00;        //UART0选择位置0 TX@P0.3 RX@P0.2
  P0SEL |= 0x0C;        //P0.3 P0.2选择外设功能，第二位第三位为1
  U0CSR |= 0xC0;        //UART模式：接收器使能
  U0GCR |= 11;          //查表获得 U0GCR 和 U0BAUD设定波特率为115200
  U0BAUD = 216;
  
  UTX0IF = 1;           //中断未决
  URX0IE = 1;           //USART0 RX接收中断使能
}

void uart0_flush_rxbuf()
{
  serial_rxpos = 0;
  serial_rxlen = 0;
}

void timer1_init()
{
  T1IE = 1;             //IEN1@BIT1 使能定时器1中断
  
  T1CTL = 0x0C;         //@DIV分频系数 128 @MODE暂停运行
  T1CCTL0 = 0x44;       //@IM通道0中断使能 @MODE比较匹配模式
  T1STAT = 0x00;
  
  T1IE = 1;             //IEN1@BIT1 使能定时器1中断
  
  T1CC0L = 250;         //溢出周期为2ms
  T1CC0H = 0;
}

void timer1_disable()
{
  T1CTL &= ~(1 << 1);
}

void timer1_enable()
{
  T1CTL |= (1 << 1);    //TIMER1@MODE 10：模，从0x0000到T1CC0反复计数
  T1STAT = 0x00;        //清除中断标志位
  T1CNTH = 0;           //重新开始计数
  T1CNTL = 0;
}

void rf_init()
{
  TXPOWER       = 0xF5;         //发射功率为4.5dBm（最大）
  CCACTRL0      = 0xF8;         //推荐值 smartRF软件生成 CCA:空闲通道评估
  
  FRMFILT0      = 0x0C;         //禁止接收过滤，即接收所有数据包
  
  FSCAL1        = 0x00;         //推荐值 smartRF软件生成
  TXFILTCFG     = 0x09;
  AGCCTRL1      =  0x15;
  AGCCTRL2      =  0xFE;

  TXFILTCFG     = 0x09;         // 推荐值 smartRF软件生成
  
  FREQCTRL      = 0x0B;         // 选择通道11

  RFIRQM0       |= (1<<6);      // 使能RF数据包接收中断
  IEN2          |= (1<<0);      // 使能RF中断

  RFST          = 0xED;         // 清除RF接收缓冲区 ISFLUSHRX
  RFST          = 0xE3;         // RF接收使能 ISRXON
}

void rf_send(char *pbuf , int len)      //射频发送数据
{
  RFST = 0xE3;                  //RF接受使能 ISRXON
  //等待发送状态不活跃，并且没有接受到SFD
  while( FSMSTAT1 & (( 1<<1 ) | ( 1<<5 )));
  
  RFIRQM0 &= ~( 1<<6 );         //禁止接收数据包中断
  IEN2 &= ~( 1<<0 );            //清除RF全局中断
  
  RFST = 0xEE;                  //清除发送缓冲区 ISFLUSHTX
  RFIRQF1 = ~( 1<<1 );          //清除发送完成标志
  
  //填充缓冲区，填充过程需要增加2字节，CRC校验自动填充
  RFD = len + 2;
  
  for (int i = 0; i < len; i++)
  {
      RFD = *pbuf++;            //写RFD，相当于写发送缓冲区
  }
  
  RFST = 0xE9;                  //发送数据包 ISTXON
  while (!(RFIRQF1 & ( 1<<1 )));        //等待发送完成
  RFIRQF1 = ~( 1<<1 );          //清除发送完成标志位
  
  RFIRQM0 |= ( 1<<6 );          //RX接收中断
  IEN2 |= ( 1<<0 );             //RF一般中断使能
  
}

void rf_receive_isr()           //接收数据包
{
  int rf_rx_len = 0;
  int rssi = 0;
  char crc_ok = 0;
  
  rf_rx_len = RFD - 2;          //长度去除2字节的附加结果
  rf_rx_len &= 0x7F;            //长度不能超过128
  for (int i = 0; i < rf_rx_len; i++)
  {
    rf_rx_buf[i] = RFD;         //连续读取缓冲区的内容
  }
  
  rssi = RFD - 73;              //读取RSSI结果 ？？？？？
  crc_ok = RFD;                 //去读crc校验结果 BIT7
  
  RFST = 0xED;                  //清除接收缓冲区 
  if( crc_ok & 0x80)
  {
    uart0_sendbuf( rf_rx_buf , rf_rx_len);      //串口发送
  }
  else
  {
    printf("\r\nCRC Error\r\n");
  }
}

void main(void)
{
  P1DIR |= ( 1<<0 )| ( 1<<1 );          // P1.0输出
  LED1 = 0;
  LED2 = 0;
  
  EA = 0;                       //暂时关闭全局中断
  
  SLEEPCMD &= ~0x04;            //设置系统时钟为32Mhz
  while( !(SLEEPSTA & 0x40));
  CLKCONCMD &= ~0x47;
  SLEEPCMD = 0x04;
  
  uart0_init();                 //串口初始化
  timer1_init();                //定时器初始化 2ms 比较匹配
  rf_init();                    //RF初始化 无帧过滤
  
  EA = 1;                       //使能全局中断
  
  printf("The system is running....\r\n");
  
  while(1)
  {
    if ( is_serial_receive)     //收到串口数据包
    {
      is_serial_receive = 0;    //清除标志位
      LED1 ^= 1;
      rf_send(serial_rxbuf , serial_rxlen);     //直接转发串口数据
      uart0_flush_rxbuf();      //清除串口发送数据
    }
  }
}
      
int putchar( int c)
{
  while( !UTX0IF);
  UTX0IF = 0;
  U0DBUF = c;
  return c;
}

void uart0_sendbuf(char *pbuf, int len)
{
  for( int i = 0; i < len ; i++ )
  {
    while( !UTX0IF);
    UTX0IF = 0;
    U0DBUF = *pbuf;
    pbuf++;
  }
}

#pragma vector=URX0_VECTOR
__interrupt void UART0_ISR(void)
{
    URX0IF = 0;                                   // 清除接收中断标志
    serial_rxbuf[serial_rxpos] = U0DBUF;          // 填充缓冲区
    serial_rxpos++;
    serial_rxlen++;

    timer1_enable();                              // 定时器重新开始计数
}


#pragma vector=T1_VECTOR
__interrupt void Timer1_ISR(void)
{
    T1STAT &= ~( 1<< 0);                          // 清除定时器T1通道0中断标志

    is_serial_receive = 1;                        // 串口数据到达
    timer1_disable();
}

#pragma vector=RF_VECTOR
__interrupt void rf_isr(void)
{
    LED2 ^= 1;                                    // LED1翻转 提示作用
    EA = 0;

    // 接收到一个完整的数据包
    if (RFIRQF0 & ( 1<<6 ))
    {
        rf_receive_isr();                           // 调用接收中断处理函数

        S1CON = 0;                                  // 清除RF中断标志
        RFIRQF0 &= ~(1<<6);                         // 清除RF接收完成数据包中断
    }
    EA = 1;
}

  
  
           
  
  
  



