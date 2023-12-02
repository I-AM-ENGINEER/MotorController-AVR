#define	F_CPU 16000UL	// Да, 16 кГц, а хули нет, и этого хватит

#include <avr/interrupt.h>
#include <avr/io.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <util/delay.h>
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <stdlib.h>
/******************** Настройки ********************/

#define LCD_PORT		PORTD
#define LCD_PIN_RS		PD6
#define LCD_PIN_E		PD4

#define SOFT_CONTROL_STEP	2

#define DIR_FORWARD		true
#define DIR_BACKWARD	false

/******************** Глобальные переменные ********************/

// U - увеличить оботоры +10%
// u - уменьшить обороты +1%
// D - уменьшить обороты -10%
// в - уменьшить обороты -1%
// m - максимальные обороты
// S - обороты в 0
// E - велючить двигатель
// S - выключить двигатель
// R - реверс

// Расположение кнопок

const char but2char[3][3] = {
	{'u', 'U', 'M'},
	{'d', 'D', 'm'},
	{'E', 'S', 'R'},
};

volatile bool enable;
volatile int8_t target_speed;
volatile bool target_direction = true;
volatile bool real_direction = true;

/******************** Драйвер дисплея ********************/

void lcd_latch(void){
	LCD_PORT |=  (1 << LCD_PIN_E); // Установить высокий уровень на тактовом пине шины
	LCD_PORT &= ~(1 << LCD_PIN_E); // Установить низкий уровень на тактовом пине шины
}

void lcd_write(uint8_t byte){
	LCD_PORT = (byte >> 4) | (LCD_PORT & 0xF0); // Записываем старшие 4 байта на шину
	lcd_latch();
	LCD_PORT = (byte & 0x0F) | (LCD_PORT & 0xF0); // Записываем младшие 4 байта на шину
	lcd_latch();
}

void lcd_write_cmd(uint8_t cmd){
	LCD_PORT &= ~(1 << LCD_PIN_RS); // Дисплей в режим команд (0 на пине RS)
	lcd_write(cmd); // Запись команды
}

void lcd_putc(uint8_t data){
	LCD_PORT |= (1 << LCD_PIN_RS); // Дисплей в режим данных
	lcd_write(data); // Запись данных
}

void lcd_set_cursor(uint8_t line, uint8_t columm){
	uint8_t position = (line << 6) | (columm); // Установка позиции курсора
	lcd_write_cmd(0x80 | position);
}

void lcd_init(void){
	// Последовательность иницаиализации из даташита
	_delay_ms(20); // Задержка, что бы дисплей успел включиться
	LCD_PORT = 0x03 | (LCD_PORT & 0xF0); // Записываем старшие 4 байта на шину
	lcd_latch();
	_delay_ms(5);
	LCD_PORT = 0x03 | (LCD_PORT & 0xF0); // Записываем старшие 4 байта на шину
	lcd_latch();
	_delay_us(100); // Выполнение записи может занять до 40мкс, ждем
	
	lcd_write_cmd(0x32);
	lcd_write_cmd(0x28);
	lcd_write_cmd(0x0C);
	lcd_write_cmd(0x06);

	lcd_write_cmd(0x01); // Команда очистки дисплея
	_delay_ms(2); // Очистка занимает до 1.52мс
}

void lcd_puts(const char *string){
	while(*string){ // Пока в строке не кончились символы, выводим
		lcd_putc(*string);
		string++;
	}
}

void btnPushedISR(char button){	
	switch (button){
		case 'R': target_direction = !target_direction; break;
		case 'E': enable = true; break;
		case 'S': enable = false; break;
		case 'u': target_speed += 1; break;
		case 'U': target_speed += 10; break;
		case 'd': target_speed -= 1; break;
		case 'D': target_speed -=10; break;
		case 'M': target_speed = 100; break;
		case 'm': target_speed = 0; break;
		default: break;
	}
	if(target_speed > 100){
		target_speed = 100;
	}else if(target_speed < 0){
		target_speed = 0;
	}
}

void update_motor_state( void ){
	if(enable){
		// Выключить ШИМ на нулевой скорости
		if(OCR1AL)
			TCCR1A |= (1 << COM1A1);
		else
			TCCR1A &= ~(1 << COM1A1);
		// Записать текущее направление движения
		if(real_direction)	
			PORTA = 0b01;
		else
			PORTA = 0b10;
		// Если надо сделать реверс, делаем реверс
		if(target_direction != real_direction){
			if(OCR1AL > SOFT_CONTROL_STEP)
				OCR1AL -= SOFT_CONTROL_STEP;
			else
				OCR1AL = 0;

			if(OCR1AL == 0)
				real_direction = target_direction;
		// Или надо поменять скорость
		}else if(target_speed != OCR1AL){
			int8_t delta = target_speed - OCR1AL;
			if(abs(delta) > SOFT_CONTROL_STEP)
				delta = delta>0?SOFT_CONTROL_STEP:-SOFT_CONTROL_STEP;
			OCR1AL += delta;
		}
	}else{
		OCR1AL = 0;
		PORTA = 0b00;
		TCCR1A &= ~(1 << COM1A1);
	}
}

void kbrd_scan( void ){
	static int8_t last_key = -1;
	for(int8_t i = 0; i < 3; i++){
		PORTB |= 0x07;
		switch (i){
			case 0: PORTB &= ~0x04; break;
			case 1: PORTB &= ~0x02; break;
			case 2: PORTB &= ~0x01; break;
			default: break;
		}
		// Если ничего не нажато, переход к следующему столбцу
		switch (PINB & 0xE0){
			case 0x60: i+=3;
			case 0xA0: i+=3;
			case 0xC0: break; 
			default:   continue; break;
		}
		
		if(i != last_key){
			last_key = i;
			btnPushedISR(*((const char*)but2char+i));
		}
		return;
	}
	last_key = -1;
}

ISR(TIMER0_COMPA_vect){
	update_motor_state();
	kbrd_scan();
}

void print_value(uint8_t val){
	if(val == 100){
		lcd_puts("100");
	}else if(val<10){
		lcd_write(' ');
		lcd_write(' ');
		lcd_write(val + '0');
	}else{
		lcd_write(' ');
		lcd_write(val/10 + '0');
		lcd_write(val%10 + '0');
	}
}

void print_direction(bool dir){
	if(dir)
		lcd_write('F');
	else
		lcd_write('B');
}

int main(void){
	DDRA = 0x03;
	DDRB = 0x0F;
	DDRD = 0xFF;
	PORTB = 0xE0;
	PORTA = 0b01;
	// Режим CTC
	TCCR0A = (1 << WGM01);
	// Делитель 8
	TCCR0B = (1 << CS01);
	// Опрос кнопок, плавное изменение частоты вращения раз 40 раз в секунду
	OCR0A = 50-1;

	// Fast PWM, без делителя, частота ШИМ 160 Гц
	TCCR1A = (1 << WGM11);
	TCCR1B = (1 << WGM12) | (1 << WGM13) | (1 << CS10);
	ICR1 = 100; // Максимальное значение ШИМ
	TIMSK = (1 << OCIE0A); // Включение прерывание по перепелнонию

	sei(); // Велючение прерываний
	
	lcd_init(); // Инициализация дисплея
	
	while (1){
		// Вывод верхней строки
		lcd_set_cursor(0,0); // Установить курсор в начало
		lcd_puts("TARGET:");
		print_value(target_speed);
		lcd_write(' ');
		print_direction(target_direction);
		lcd_write(' ');
		if(enable)
			lcd_puts("ON ");
		else
			lcd_puts("OFF");
		// Вывод нижней строки
		lcd_set_cursor(1,0); // Установить курсор в начало 2 строки
		lcd_puts("REAL:  ");
		print_value(OCR1AL);
		lcd_write(' ');
		print_direction(real_direction);
	}
}
