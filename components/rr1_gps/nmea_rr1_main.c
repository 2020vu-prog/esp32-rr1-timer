/* NMEA parsing example for ESP32.
 * Based on "parse_stdin.c" example from libnmea.
 * Copyright (c) 2015 Jack Engqvist Johansson.
 * Additions Copyright (c) 2017 Ivan Grokhotkov.
 * See "LICENSE" file in libnmea directory for license.
 */

#include "esp_log.h"
#include "gpgga.h"
#include "gpgll.h"
#include "gpgsa.h"
#include "gpgsv.h"
#include "gprmc.h"
#include "gptxt.h"
#include "gpvtg.h"
#include "nmea.h"
#include "nmea_rr1.h"
#include <stdio.h>

static void read_and_parse_nmea();

static const char *TAG = "nmea_main";
void nmea_main(void *pvParameter) {
  ESP_LOGI(TAG, "Initializing NMEA ");
  nmea_rr1_init_interface();
  read_and_parse_nmea();
}

static void read_and_parse_nmea() {
  ESP_LOGI(TAG, "Reading and parsing NMEA sentences");
  while (1) {
    char fmt_buf[32];
    nmea_s *data;

    char *start;
    size_t length;
    // nmea_rr1_read_line(&start, &length, 100 /* ms */);
    nmea_rr1_read_line(&start, &length, 250 /* ms */);

    if (length == 0) {
      continue;
    }

    /* handle data */
    data = nmea_parse(start, length, 0);
    if (data == NULL) {
      ESP_LOGI(TAG, "Failed to parse the sentence!");
      ESP_LOGI(TAG, "  Type: %.5s (%d)", start + 1, nmea_get_type(start));
    } else {
      if (data->errors != 0) {
        ESP_LOGI(TAG, "WARN: The sentence struct contains parse errors!	");
      }

      if (NMEA_GPGGA == data->type) {
        ESP_LOGI(TAG, "GPGGA sentence");
        nmea_gpgga_s *gpgga = (nmea_gpgga_s *)data;
        ESP_LOGI(TAG, "Number of satellites: %d", gpgga->n_satellites);
        ESP_LOGI(TAG, "Altitude: %f %c", gpgga->altitude, gpgga->altitude_unit);
      }

      if (NMEA_GPGLL == data->type) {
        ESP_LOGI(TAG, "GPGLL sentence");
        nmea_gpgll_s *pos = (nmea_gpgll_s *)data;
        ESP_LOGI(TAG, "Longitude:\n");
        ESP_LOGI(TAG, "  Degrees: %d\n", pos->longitude.degrees);
        ESP_LOGI(TAG, "  Minutes: %f\n", pos->longitude.minutes);
        ESP_LOGI(TAG, "  Cardinal: %c\n", (char)pos->longitude.cardinal);
        ESP_LOGI(TAG, "Latitude:\n");
        ESP_LOGI(TAG, "  Degrees: %d\n", pos->latitude.degrees);
        ESP_LOGI(TAG, "  Minutes: %f\n", pos->latitude.minutes);
        ESP_LOGI(TAG, "  Cardinal: %c\n", (char)pos->latitude.cardinal);
        strftime(fmt_buf, sizeof(fmt_buf), "%H:%M:%S", &pos->time);
        ESP_LOGI(TAG, "Time: %s\n", fmt_buf);
      }

      if (NMEA_GPRMC == data->type) {
        ESP_LOGI(TAG, "GPRMC sentence");
        nmea_gprmc_s *pos = (nmea_gprmc_s *)data;
        ESP_LOGI(TAG, "Longitude:\n");
        ESP_LOGI(TAG, "  Degrees: %d\n", pos->longitude.degrees);
        ESP_LOGI(TAG, "  Minutes: %f\n", pos->longitude.minutes);
        ESP_LOGI(TAG, "  Cardinal: %c\n", (char)pos->longitude.cardinal);
        ESP_LOGI(TAG, "Latitude:\n");
        ESP_LOGI(TAG, "  Degrees: %d\n", pos->latitude.degrees);
        ESP_LOGI(TAG, "  Minutes: %f\n", pos->latitude.minutes);
        ESP_LOGI(TAG, "  Cardinal: %c\n", (char)pos->latitude.cardinal);
        strftime(fmt_buf, sizeof(fmt_buf), "%d %b %T %Y", &pos->date_time);
        ESP_LOGI(TAG, "Date & Time: %s\n", fmt_buf);
        ESP_LOGI(TAG, "Speed, in Knots: %f\n", pos->gndspd_knots);
        ESP_LOGI(TAG, "Track, in degrees: %f\n", pos->track_deg);
        ESP_LOGI(TAG, "Magnetic Variation:\n");
        ESP_LOGI(TAG, "  Degrees: %f\n", pos->magvar_deg);
        ESP_LOGI(TAG, "  Cardinal: %c\n", (char)pos->magvar_cardinal);
        double adjusted_course = pos->track_deg;
        if (NMEA_CARDINAL_DIR_EAST == pos->magvar_cardinal) {
          adjusted_course -= pos->magvar_deg;
        } else if (NMEA_CARDINAL_DIR_WEST == pos->magvar_cardinal) {
          adjusted_course += pos->magvar_deg;
        } else {
          ESP_LOGI(TAG, "Invalid Magnetic Variation Direction!\n");
        }

        ESP_LOGI(TAG, "Adjusted Track (heading): %f\n", adjusted_course);
      }

      if (NMEA_GPGSA == data->type) {
        nmea_gpgsa_s *gpgsa = (nmea_gpgsa_s *)data;

        ESP_LOGI(TAG, "GPGSA Sentence:\n");
        ESP_LOGI(TAG, "  Mode: %c\n", gpgsa->mode);
        ESP_LOGI(TAG, "  Fix:  %d\n", gpgsa->fixtype);
        ESP_LOGI(TAG, "  PDOP: %.2lf\n", gpgsa->pdop);
        ESP_LOGI(TAG, "  HDOP: %.2lf\n", gpgsa->hdop);
        ESP_LOGI(TAG, "  VDOP: %.2lf\n", gpgsa->vdop);
      }

      if (NMEA_GPGSV == data->type) {
        nmea_gpgsv_s *gpgsv = (nmea_gpgsv_s *)data;

        ESP_LOGI(TAG, "GPGSV Sentence:\n");
        ESP_LOGI(TAG, "  Num: %d\n", gpgsv->sentences);
        ESP_LOGI(TAG, "  ID:  %d\n", gpgsv->sentence_number);
        ESP_LOGI(TAG, "  SV:  %d\n", gpgsv->satellites);
        ESP_LOGI(TAG, "  #1:  %d %d %d %d\n", gpgsv->sat[0].prn,
                 gpgsv->sat[0].elevation, gpgsv->sat[0].azimuth,
                 gpgsv->sat[0].snr);
        ESP_LOGI(TAG, "  #2:  %d %d %d %d\n", gpgsv->sat[1].prn,
                 gpgsv->sat[1].elevation, gpgsv->sat[1].azimuth,
                 gpgsv->sat[1].snr);
        ESP_LOGI(TAG, "  #3:  %d %d %d %d\n", gpgsv->sat[2].prn,
                 gpgsv->sat[2].elevation, gpgsv->sat[2].azimuth,
                 gpgsv->sat[2].snr);
        ESP_LOGI(TAG, "  #4:  %d %d %d %d\n", gpgsv->sat[3].prn,
                 gpgsv->sat[3].elevation, gpgsv->sat[3].azimuth,
                 gpgsv->sat[3].snr);
      }

      if (NMEA_GPTXT == data->type) {
        nmea_gptxt_s *gptxt = (nmea_gptxt_s *)data;

        printf("GPTXT Sentence:\n");
        printf("  ID: %d %d %d\n", gptxt->id_00, gptxt->id_01, gptxt->id_02);
        printf("  %s\n", gptxt->text);
      }

      if (NMEA_GPVTG == data->type) {
        nmea_gpvtg_s *gpvtg = (nmea_gpvtg_s *)data;

        printf("GPVTG Sentence:\n");
        printf("  Track [deg]:   %.2lf\n", gpvtg->track_deg);
        printf("  Speed [kmph]:  %.2lf\n", gpvtg->gndspd_kmph);
        printf("  Speed [knots]: %.2lf\n", gpvtg->gndspd_knots);
      }

      nmea_free(data);
    }
  }
}