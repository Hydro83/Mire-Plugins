CC = gcc
CXX = g++
CFLAGS = -shared -fPIC -O3 -ffast-math -lm
LADSPA_DIR = LADSPA
LV2_DIR = LV2

INSTALL_LADSPA = $(HOME)/.ladspa
INSTALL_LV2 = $(HOME)/.lv2


LADSPA_SOURCES = $(wildcard $(LADSPA_DIR)/*.c)
LADSPA_PLUGINS = $(LADSPA_SOURCES:.c=.so)

all: ladspa lv2


ladspa: $(LADSPA_PLUGINS)

$(LADSPA_DIR)/%.so: $(LADSPA_DIR)/%.c
	$(CC) $(CFLAGS) -o $@ $<

lv2:
	$(CXX) -shared -fPIC -O3 -o $(LV2_DIR)/KickDruMan.lv2/KickDruMan.so $(LV2_DIR)/KickDruMan.lv2/KickDruMan.cpp -ffast-math -lm
	$(CXX) -shared -fPIC -O3 -o $(LV2_DIR)/MireBass.lv2/MireBass.so $(LV2_DIR)/MireBass.lv2/MireBass.cpp -ffast-math -lm


install:
	@mkdir -p $(INSTALL_LADSPA)
	@mkdir -p $(INSTALL_LV2)
	
	@echo "Installing LADSPA plugins to $(INSTALL_LADSPA)..."
	cp $(LADSPA_DIR)/*.so $(INSTALL_LADSPA)/
	
	@echo "Installing LV2 bundles to $(INSTALL_LV2)..."
	@for dir in $(LV2_DIR)/*.lv2; do \
		name=$$(basename $$dir); \
		mkdir -p $(INSTALL_LV2)/$$name; \
		cp $$dir/*.so $(INSTALL_LV2)/$$name/; \
		cp $$dir/*.ttl $(INSTALL_LV2)/$$name/; \
	done
	
	@echo "Done!"

clean:
	rm -f $(LADSPA_DIR)/*.so
	rm -f $(LV2_DIR)/**/*.so