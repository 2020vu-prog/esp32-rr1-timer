/* NMEA parsing example for ESP32.
 * Based on "parse_stdin.c" example from libnmea.
 * Copyright (c) 2015 Jack Engqvist Johansson.
 * Additions Copyright (c) 2017 Ivan Grokhotkov.
 * See "LICENSE" file in libnmea directory for license.
 */

#include "esp_log.h"
#include "esp_timer.h"

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
#include <time.h>

static void read_and_parse_nmea();

static const char *TAG = "nmea_main";
#include <math.h>
#include <stdint.h>
#include <stdio.h>

/**
 * Converts Degrees and Decimal Minutes to a 64-bit scaled integer.
 * @param degrees The whole degrees part.
 * @param minutes The decimal minutes part.
 * @param is_negative Set to 1 for West/South, 0 for East/North.
 * @return A 64-bit integer scaled by 1,000,000,000.
 */
int64_t ddm_to_int64(nmea_position *pos) {
  // 1. Convert DDM to Decimal Degrees (DD)
  double dd = (double)pos->degrees + (pos->minutes / 60.0);

  // 3. Scale to 64-bit integer (using 10^9 for nano-degree precision)
  // Precision: ~0.1mm at the equator
  const double SCALE = 1000000000.0;

  int64_t rc = (int64_t)round(dd * SCALE);
  if (pos->cardinal == NMEA_CARDINAL_DIR_WEST ||
      pos->cardinal == NMEA_CARDINAL_DIR_SOUTH) {
    rc *= -1;
  }
  return rc;
}

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
    uint64_t nowSecs = esp_timer_get_time() / (1000 * 1000);

    if (nowSecs % 10 != 0) {
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
        // ESP_LOGI(TAG, "Altitude: %f %c", gpgga->altitude,
        // gpgga->altitude_unit);
      }

      if (NMEA_GPGLL == data->type) {
        ESP_LOGI(TAG, "GPGLL sentence");
        nmea_gpgll_s *pos = (nmea_gpgll_s *)data;
        ESP_LOGI(TAG, "Longitude:");
        ESP_LOGI(TAG, "  Degrees: %d", pos->longitude.degrees);
        ESP_LOGI(TAG, "  Minutes: %f", pos->longitude.minutes);
        ESP_LOGI(TAG, "  Cardinal: %c", (char)pos->longitude.cardinal);
        ESP_LOGI(TAG, "Latitude:");
        ESP_LOGI(TAG, "  Degrees: %d", pos->latitude.degrees);
        ESP_LOGI(TAG, "  Minutes: %f", pos->latitude.minutes);
        ESP_LOGI(TAG, "  Cardinal: %c", (char)pos->latitude.cardinal);
        ESP_LOGI(TAG, "lat %" PRId64 " ,%" PRId64, ddm_to_int64(&pos->latitude),
                 ddm_to_int64(&pos->longitude));
        strftime(fmt_buf, sizeof(fmt_buf), "%Y:%m:%d %H:%M:%S", &pos->time);
        ESP_LOGI(TAG, "Time: %s", fmt_buf);
        time_t gps_epoch_utc = mktime(&pos->time);
        time_t now;
        time(&now);
        int delta = now - gps_epoch_utc;
        ESP_LOGI(TAG, "GPGLL Delta %d", delta);
      }

      if (NMEA_GPRMC == data->type) {
        ESP_LOGI(TAG, "GPRMC sentence");
        nmea_gprmc_s *pos = (nmea_gprmc_s *)data;
        ESP_LOGI(TAG, "Longitude:");
        ESP_LOGI(TAG, "  Degrees: %d", pos->longitude.degrees);
        ESP_LOGI(TAG, "  Minutes: %f", pos->longitude.minutes);
        ESP_LOGI(TAG, "  Cardinal: %c", (char)pos->longitude.cardinal);
        ESP_LOGI(TAG, "Latitude:");
        ESP_LOGI(TAG, "  Degrees: %d", pos->latitude.degrees);
        ESP_LOGI(TAG, "  Minutes: %f", pos->latitude.minutes);
        ESP_LOGI(TAG, "  Cardinal: %c", (char)pos->latitude.cardinal);
        strftime(fmt_buf, sizeof(fmt_buf), "%d %b %T %Y", &pos->date_time);
        ESP_LOGI(TAG, "Date & Time: %s", fmt_buf);

        time_t gps_epoch_utc = mktime(&pos->date_time);
        time_t now;
        time(&now);
        int delta = now - gps_epoch_utc;
        ESP_LOGW(TAG, "GPRMC Delta %d valid %d", delta, (int)(&pos->valid));

        ESP_LOGI(TAG, "Speed, in Knots: %f", pos->gndspd_knots);
        ESP_LOGI(TAG, "Track, in degrees: %f", pos->track_deg);
        ESP_LOGI(TAG, "Magnetic Variation:");
        ESP_LOGI(TAG, "  Degrees: %f", pos->magvar_deg);
        ESP_LOGI(TAG, "  Cardinal: %c", (char)pos->magvar_cardinal);
        double adjusted_course = pos->track_deg;
        if (NMEA_CARDINAL_DIR_EAST == pos->magvar_cardinal) {
          adjusted_course -= pos->magvar_deg;
        } else if (NMEA_CARDINAL_DIR_WEST == pos->magvar_cardinal) {
          adjusted_course += pos->magvar_deg;
        } else {
          ESP_LOGI(TAG, "Invalid Magnetic Variation Direction!");
        }

        ESP_LOGI(TAG, "Adjusted Track (heading): %f", adjusted_course);
      }

      if (false && NMEA_GPGSA == data->type) {
        nmea_gpgsa_s *gpgsa = (nmea_gpgsa_s *)data;

        ESP_LOGI(TAG, "GPGSA Sentence:");
        ESP_LOGI(TAG, "  Mode: %c", gpgsa->mode);
        ESP_LOGI(TAG, "  Fix:  %d", gpgsa->fixtype);
        ESP_LOGI(TAG, "  PDOP: %.2lf", gpgsa->pdop);
        ESP_LOGI(TAG, "  HDOP: %.2lf", gpgsa->hdop);
        ESP_LOGI(TAG, "  VDOP: %.2lf", gpgsa->vdop);
      }

      if (false && NMEA_GPGSV == data->type) {
        nmea_gpgsv_s *gpgsv = (nmea_gpgsv_s *)data;

        ESP_LOGI(TAG, "GPGSV Sentence:");
        ESP_LOGI(TAG, "  Num: %d", gpgsv->sentences);
        ESP_LOGI(TAG, "  ID:  %d", gpgsv->sentence_number);
        ESP_LOGI(TAG, "  SV:  %d", gpgsv->satellites);
        ESP_LOGI(TAG, "  #1:  %d %d %d %d", gpgsv->sat[0].prn,
                 gpgsv->sat[0].elevation, gpgsv->sat[0].azimuth,
                 gpgsv->sat[0].snr);
        ESP_LOGI(TAG, "  #2:  %d %d %d %d", gpgsv->sat[1].prn,
                 gpgsv->sat[1].elevation, gpgsv->sat[1].azimuth,
                 gpgsv->sat[1].snr);
        ESP_LOGI(TAG, "  #3:  %d %d %d %d", gpgsv->sat[2].prn,
                 gpgsv->sat[2].elevation, gpgsv->sat[2].azimuth,
                 gpgsv->sat[2].snr);
        ESP_LOGI(TAG, "  #4:  %d %d %d %d", gpgsv->sat[3].prn,
                 gpgsv->sat[3].elevation, gpgsv->sat[3].azimuth,
                 gpgsv->sat[3].snr);
      }

      if (NMEA_GPTXT == data->type) {
        nmea_gptxt_s *gptxt = (nmea_gptxt_s *)data;

        ESP_LOGI(TAG, "GPTXT Sentence:");
        ESP_LOGI(TAG, "  ID: %d %d %d", gptxt->id_00, gptxt->id_01,
                 gptxt->id_02);
        ESP_LOGI(TAG, "  %s", gptxt->text);
      }

      if (false && NMEA_GPVTG == data->type) {
        nmea_gpvtg_s *gpvtg = (nmea_gpvtg_s *)data;

        ESP_LOGI(TAG, "GPVTG Sentence:");
        ESP_LOGI(TAG, "  Track [deg]:   %.2lf", gpvtg->track_deg);
        ESP_LOGI(TAG, "  Speed [kmph]:  %.2lf", gpvtg->gndspd_kmph);
        ESP_LOGI(TAG, "  Speed [knots]: %.2lf", gpvtg->gndspd_knots);
      }

      nmea_free(data);
    }
  }
}