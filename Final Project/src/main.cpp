#include <mbed.h>
#include "drivers/LCD_DISCO_F429ZI.h"
#include "i2c.h"
#define HAdd (0x18 << 1)
#define BACKGROUND 1
#define FOREGROUND 0
#define GRAPH_PADDING 5


LCD_DISCO_F429ZI lcd;
InterruptIn buttonInterrupt(USER_BUTTON,PullDown);

//buffer for holding displayed text strings
char display_buf[2][60];
uint32_t graph_width=lcd.GetXSize()-2*GRAPH_PADDING;
uint32_t graph_height=graph_width;

// Screen Setup
//------------------------------------------------------------------------------------------------------------------------------
//sets the background layer 
//to be visible, transparent, and
//resets its colors to all black
void setup_background_layer(){
  lcd.SelectLayer(BACKGROUND);
  lcd.Clear(LCD_COLOR_BLACK);
  lcd.SetBackColor(LCD_COLOR_BLACK);
  lcd.SetTextColor(LCD_COLOR_GREEN);
  lcd.SetLayerVisible(BACKGROUND,ENABLE);
  lcd.SetTransparency(BACKGROUND,0x7Fu);
}

//resets the foreground layer to
//all black
void setup_foreground_layer(){
    lcd.SelectLayer(FOREGROUND);
    lcd.Clear(LCD_COLOR_BLACK);
    lcd.SetBackColor(LCD_COLOR_BLACK);
    lcd.SetTextColor(LCD_COLOR_LIGHTGREEN);
}

//draws a rectangle with horizontal tick marks
//on the background layer. The spacing between tick
//marks in pixels is taken as a parameter
void draw_graph_window(uint32_t horiz_tick_spacing){
  lcd.SelectLayer(BACKGROUND);
  
  lcd.DrawRect(GRAPH_PADDING,GRAPH_PADDING,graph_width,graph_width);
  //draw the x-axis tick marks
  for (uint32_t i = 0 ; i < graph_width;i+=horiz_tick_spacing){
    lcd.DrawVLine(GRAPH_PADDING+i,graph_height,GRAPH_PADDING);
  }
}

//maps inputY in the range minVal to maxVal, to a y-axis value pixel in the range
//minPixelY to MaxPixelY
uint16_t mapPixelY(float inputY,float minVal, float maxVal, int32_t minPixelY, int32_t maxPixelY){
  const float mapped_pixel_y=(float)maxPixelY-(inputY)/(maxVal-minVal)*((float)maxPixelY-(float)minPixelY);
  return mapped_pixel_y;
}

//----------------------------------------------------------------------------------------------------------------------------------

I2C MyI2C(PC_9,PA_8);  //SDA, SCL

char tx[] = {0xAA,0x00,0x00};

char rx[4];


void button_pressed(void) {
    lcd.Clear(LCD_COLOR_BLACK); //clearing lcd screen so the program can tell the user to start pumping
    lcd.SetTextColor(LCD_COLOR_BLUE); //changing the text color
    lcd.ClearStringLine(LINE(8)); //clearing the previous string output
    snprintf(display_buf[0],100,"Start Pumping!!");
    lcd.SelectLayer(FOREGROUND);
    lcd.DisplayStringAt(0, LINE(8), (uint8_t *)display_buf[0], CENTER_MODE);
    get_data();
}

void get_data(void){
    buttonInterrupt.rise(NULL); //disable the interrupt
    double converted_pressure = 0.0;
    int index = 0;
    bool too_fast = false;
    double data[100000];

    int num = MyI2C.write(HAdd, tx, 3); //write to sensor
    thread_sleep_for(15); //wait for 15 seconds
    int pressure = MyI2C.read(HAdd, rx, 1); //read from the senor to get the pressure value
    converted_pressure = converting_pressure_value(pressure); //convert the value to pressure in mmhg
    data[index] = converted_pressure;

  while(converted_pressure < 150.0) { //keep getting data until the pressure is 150mmhg
      //get value from the sensor
      index += 1;
      int num = MyI2C.write(HAdd, tx, 3);
      thread_sleep_for(15);
      int pressure = MyI2C.read(HAdd, rx, 4);
      converted_pressure = converting_pressure_value(pressure);

      if ((converted_pressure- data[index-1]) > 4){ //check if the user is pumping too fast
          too_fast = true;
          break;
      }
      data[index] = converted_pressure;
      thread_sleep_for(125); //get the prssure every 8th of a second     
  }

  if(too_fast){//if true call the restart function
      restart();
      
  }
  else{
    find_systolic_blood_pressure(*data);
  }

}



double converting_pressure_value(int data){
    double output_max =  3774873.6; //value determined from the data sheet (22.5% of 2^24)
    double output_min =  419430.4; //value determined from the data sheet (2.5% of 2^24)
    int pmax = 1; //value given in the data sheet
    int pmin = -1; //value given in the data sheet
    int output = data; //value from the sensor
    int pressure = (((output_max-output_min)*(pmax-pmin))/(output_max - output_min))+pmin; //transfer function given in the data sheet
    //the value of the pressure is now in psi so wee need to convert it to mmhg
    double converted_pressure = pressure*51.715;
    return converted_pressure; //return value

}


double* moving_average_filter(double *data) {
    int size_of_data = (sizeof(*data)/sizeof(data[0])); //getting the length of the data
    double y[size_of_data];
    for( i = 0; i < size_of_data; i++) {//implementing a 5 point moving average filter 
       if(i == 0){
             y[i] = (1/5)*data[i];
        }
      elseif( i == 1){
             y[i] = (1/5)*data[i] + (1/5)*data[i-1];
      }
      elseif( i == 2){
             y[i] = (1/5)*data[i] + (1/5)*data[i-1] + + (1/5)*data[i-2];
      }
      elseif( i == 3){
             y[i] = (1/5)*data[i] + (1/5)*data[i-1] + (1/5)*data[i-2] + (1/5)*data[i-3];
      }
      else{
            y[i] = (1/5)*data[i] + (1/5)*data[i-1] + (1/5)*data[i-2] + (1/5)*data[i-3]+ (1/5)*data[i-4];
      }
    
    return y;
}

void find_systolic_blood_pressure(double *data){
    int size_of_data = sizeof(data)/sizeof(data[0]); //getting the length of the data
    double y[] = moving_average_filter(double *data);
    double systolic_blood_pressure = 0.0;
    bool wave_start = true;
    double filtered_data[size_of_data];

    for(i = 0; i < size_of_data; i++){//subtracting the n the filtered signal from the original signal
        double value = data[i] - y[i]; 
        filtered_data[i] = value;
        if ((value > 0) && (wave_start)){// finding the area where there pulses begin, this is the systolic_blood_pressure
          systolic_blood_pressure = data[i];
          wave_start = false;
        }
    }
get_other_information(*data, systolic_blood_pressure, *filtered_data);
}

void get_other_information(double *data, double systolic_blood_pressure, double *filtered_data){//get the dialositic blood pressure and heart rate
  int size_of_data = sizeof(data)/sizeof(data[0]);
  double max_value = 0;
  double highest_peak = 0;

  for (i = 0; i < size_of_data; i++;){
    if (filtered_data[i] > max_value){ //find the point with the highest value in the filtered data then set the corresponding pressure value as the highest peak
        highest_peak = data[i];
    }
  }

  double diastolic_blood_pressure = highest_peak - (highest_peak - systolic_blood_pressure); //get the distolic blood pressure
  int first_peak_index = 0;
  bool first_peak_found = false;
  int second_peak_index = 0;
  bool second_peak_found = false;

  for (i = 0; i < size_of_data; i++;){// find the space between two peaks to get the heart rate
      if (i == 0){
        if (filtered_data[i] > filtered_data[i + 1]){ //edge case 
          first_peak_index = i;
          first_peak_found = true;
        }
      }
      elif(i == (size_of_data - 1)){
          if (filtered_data[i] > filtered_data[i -1]){ //edge case
          second_peak_index = i;
          second_peak_found = true;
        }
      }
      else{
        if((filtered_data[i] > filtered_data[i + 1]) &&(filtered_data[i] > filtered_data[i - 1])){ //a peak will be greater than the values that come before and after it
            if(!first_peak_found){
                first_peak_index = i;
                first_peak_found = true;
            }

            elseif(!second_peak_found){
                second_peak_index = i;
                second_peak_found = true;
            }

        }

      }
  }

  int heart_rate = ((second_peak_index - first_peak_index) * .125) * 60;

  display_values(systolic_blood_pressure, diastolic_blood_pressure, heart_rate);
  
}

void display_values(double systolic_blood_pressure, double diastolic_blood_pressure, int heart_rate){ //displaying the final values
  lcd.Clear(LCD_COLOR_BLACK); //clearing lcd screen so the program can tell the user to start pumping
    lcd.SetTextColor(LCD_COLOR_BLUE); //changing the text color
    lcd.ClearStringLine(LINE(8)); //clearing the previous string output
    snprintf(display_buf[0],100,"You're Blood Pressure is %f/%f", systolic_blood_pressure, diastolic_blood_pressure);
    snprintf(display_buf[1],100,"You're Heart Rate is ", heart_rate, "beats per minute");
    lcd.SelectLayer(FOREGROUND);
    lcd.DisplayStringAt(0, LINE(8), (uint8_t *)display_buf[0], CENTER_MODE);
    lcd.DisplayStringAt(0, LINE(9), (uint8_t *)display_buf[1], CENTER_MODE);
}

void restart(void) { //tell the user they will restart becasue they're pumping to fast
    lcd.Clear(LCD_COLOR_BLACK); //clearing lcd screen so the program can tell the user to start pumping
    lcd.SetTextColor(LCD_COLOR_BLUE); //changing the text color
    lcd.ClearStringLine(LINE(8)); //clearing the previous string output
    snprintf(display_buf[0],100,"You're Pumping Too Fast!! You will restart");
    lcd.SelectLayer(FOREGROUND);
    lcd.DisplayStringAt(0, LINE(8), (uint8_t *)display_buf[0], CENTER_MODE);

}

int main(){

setup_background_layer();

setup_foreground_layer();

while(1){
    lcd.Clear(LCD_COLOR_BLACK);
    lcd.ClearStringLine(LINE(8)); //clearing the previous string output
    lcd.SetTextColor(LCD_COLOR_GREEN); //changing the text color
    snprintf(display_buf[0],60,"Press button to Start"); //string to display on the LCD
    lcd.SelectLayer(FOREGROUND);
    //display the buffered string on the screen
    lcd.DisplayStringAt(0, LINE(8), (uint8_t *)display_buf[0], LEFT_MODE);
    buttonInterrupt.rise(&button_pressed); //enable the button interrupt
    thread_sleep_for(10000); 
}

  
}
  
