#include <sys/param.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <dev/isa/isavar.h>
#include <dev/isa/isareg.h>

#include <sys/device.h>
#include <sys/gpio.h>
#include <dev/gpio/gpiovar.h>



/* GPIO Base address: AcpiMMioAddr + 0x100 */
#define	APUGPIO_ACPIMMIOADDR_BASE	0xfed80000 
#define	APUGPIO_GPIO_BASE		(APUGPIO_ACPIMMIOADDR_BASE + 0x100)


/* Pushbutton switch address (G187)  */
#define	APUGPIO_BTN_BASE		(APUGPIO_GPIO_BASE + 0xbb) 

#define APUGPIO_BTN_PRESSED		0x28	/* Button pressed value */
#define APUGPIO_BTN_UNPRESSED		0xa8	/* Button unpressed value */


/* LEDs (G189, G190, G191) */
/* Base address of first LED address */
#define	APUGPIO_LEDS_BASE		(APUGPIO_GPIO_BASE + 0xbd)

#define	APUGPIO_NLEDS			3	/* Number of LEDs */
#define APUGPIO_LED_ON			0x8	/* LED ON value */
#define APUGPIO_LED_OFF			0xc8	/* LED OFF value */


extern char *hw_vendor, *hw_prod, *hw_ver;


struct apugpio_softc {
	struct device		 sc_dev;
	bus_space_tag_t		 sc_iot;

	/* Pushbutton switch GPIO */
	bus_space_tag_t		 sc_button_iot;
	bus_space_handle_t	 sc_button_ioh;

	struct gpio_chipset_tag	 sc_button_gc;
	gpio_pin_t		 sc_button_pin;

	/* LEDs GPIO */
	bus_space_tag_t		 sc_led_iot;
	bus_space_handle_t	 sc_led_ioh;

	struct gpio_chipset_tag	 sc_led_gc;
	gpio_pin_t		 sc_led_pins[APUGPIO_NLEDS];
};

int	 apugpio_match(struct device *, void *, void *);
void	 apugpio_attach(struct device *, struct device *, void *);
void	 apugpio_ctl(void *, int, int);

int	 apugpio_led_read(void *, int);
void	 apugpio_led_write(void *, int, int);
void	 apugpio_led_ctl(void *, int, int);


int	 apugpio_button_read(void *, int);
void	 apugpio_button_write(void *, int, int);
void	 apugpio_button_ctl(void *, int, int);



struct cfattach apugpio_ca = {
	sizeof(struct apugpio_softc), apugpio_match, apugpio_attach
};

struct cfdriver apugpio_cd = {
	NULL, "apugpio", DV_DULL
};

int
apugpio_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_handle_t ioh1, ioh2;


	if (hw_vendor == NULL || hw_prod == NULL ||
	    strcmp(hw_vendor, "PC Engines") != 0 ||
	    strcmp(hw_prod, "APU") != 0 ||
	    strcmp(hw_ver, "1.0") != 0)
		return (0);

        if (ia->ia_maddr != APUGPIO_ACPIMMIOADDR_BASE ||
	    bus_space_map(ia->ia_memt, 
	    APUGPIO_BTN_BASE, 1, 0, &ioh1) != 0 ||
	    bus_space_map(ia->ia_memt, 
	        APUGPIO_LEDS_BASE, APUGPIO_NLEDS, 0, &ioh2) != 0)
		return (0);

	bus_space_unmap(ia->ia_memt, ioh1, 1);
	bus_space_unmap(ia->ia_memt, ioh2, APUGPIO_NLEDS);

	ia->ia_iosize = 0;
	ia->ia_msize = 0;

	return 1;
}

void
apugpio_attach(struct device *parent, struct device *self, void *aux)
{
	struct apugpio_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	struct gpiobus_attach_args gba1, gba2;
	int i;


	sc->sc_iot = ia->ia_memt;

	if (bus_space_map(sc->sc_iot, APUGPIO_BTN_BASE, 1, 0,
	    &sc->sc_button_ioh) != 0) {
		printf(": can't map button switch i/o space\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, APUGPIO_LEDS_BASE, APUGPIO_NLEDS, 0,
	    &sc->sc_led_ioh) != 0) {
		bus_space_unmap(sc->sc_iot, sc->sc_button_ioh, 1);
		printf(": can't map leds i/o space\n");
		return;
	}

	printf("\n");


	/* Configure pushbutton switch */
	sc->sc_button_pin.pin_num = 0;
	sc->sc_button_pin.pin_caps = GPIO_PIN_OUTPUT;
	sc->sc_button_pin.pin_flags = GPIO_PIN_OUTPUT;
	sc->sc_button_pin.pin_state = apugpio_button_read(sc, i);
	
	sc->sc_button_gc.gp_cookie = sc;
	sc->sc_button_gc.gp_pin_read = apugpio_button_read;
	sc->sc_button_gc.gp_pin_write = apugpio_button_write;
	sc->sc_button_gc.gp_pin_ctl = apugpio_button_ctl;

	gba1.gba_name = "gpio";
	gba1.gba_gc = &sc->sc_button_gc;
	gba1.gba_pins = &sc->sc_button_pin;
	gba1.gba_npins = 1;

	(void)config_found(&sc->sc_dev, &gba1, gpiobus_print);


	/* Configure LEDs */
	for (i = 0; i < APUGPIO_NLEDS; i++) {
		sc->sc_led_pins[i].pin_num = i;
		sc->sc_led_pins[i].pin_caps = GPIO_PIN_OUTPUT;
		sc->sc_led_pins[i].pin_flags = GPIO_PIN_OUTPUT;
		sc->sc_led_pins[i].pin_state = apugpio_led_read(sc, i);
	}

	sc->sc_led_gc.gp_cookie = sc;
	sc->sc_led_gc.gp_pin_read = apugpio_led_read;
	sc->sc_led_gc.gp_pin_write = apugpio_led_write;
	sc->sc_led_gc.gp_pin_ctl = apugpio_led_ctl;

	gba2.gba_name = "gpio";
	gba2.gba_gc = &sc->sc_led_gc;
	gba2.gba_pins = sc->sc_led_pins;
	gba2.gba_npins = APUGPIO_NLEDS;

	(void)config_found(&sc->sc_dev, &gba2, gpiobus_print);
}


/* LEDs routines */
int
apugpio_led_read(void *arg, int pin)
{
	struct apugpio_softc *sc = arg;
	u_int8_t value;

	value = bus_space_read_1(sc->sc_iot, sc->sc_led_ioh, pin);
	value = (value == APUGPIO_LED_ON) ? GPIO_PIN_HIGH : GPIO_PIN_LOW;

        return value;
}

void
apugpio_led_write(void *arg, int pin, int value)
{
	struct apugpio_softc *sc = arg;

	value = (value == GPIO_PIN_HIGH) ? APUGPIO_LED_ON : APUGPIO_LED_OFF;
	bus_space_write_1(sc->sc_iot, sc->sc_led_ioh, pin, value);
}

void
apugpio_led_ctl(void *arg, int pin, int flags)
{
}


/* Button routines */
int
apugpio_button_read(void *arg, int pin)
{

	struct apugpio_softc *sc = arg;
	u_int8_t value;

	value = bus_space_read_1(sc->sc_iot, sc->sc_button_ioh, 0);
	value = (value == APUGPIO_BTN_PRESSED) ? GPIO_PIN_HIGH : GPIO_PIN_LOW;

        return value;
}

void
apugpio_button_write(void *arg, int pin, int value)
{
}

void
apugpio_button_ctl(void *arg, int pin, int flags)
{
}
