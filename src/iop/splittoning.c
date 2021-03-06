/*
    This file is part of darktable,
    Copyright (C) 2010-2020 darktable developers.

    darktable is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    darktable is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with darktable.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "bauhaus/bauhaus.h"
#include "common/colorspaces.h"
#include "common/debug.h"
#include "common/opencl.h"
#include "control/control.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "dtgtk/button.h"
#include "dtgtk/gradientslider.h"
#include "dtgtk/resetlabel.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "gui/presets.h"
#include "gui/color_picker_proxy.h"
#include "iop/iop_api.h"
#include <assert.h>
#include <gtk/gtk.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CLIP(x) ((x < 0) ? 0.0 : (x > 1.0) ? 1.0 : x)
DT_MODULE_INTROSPECTION(1, dt_iop_splittoning_params_t)

typedef struct dt_iop_splittoning_params_t
{
  float shadow_hue;
  float shadow_saturation;
  float highlight_hue;
  float highlight_saturation;
  float balance;  // center luminance of gradient
  float compress; // Compress range
} dt_iop_splittoning_params_t;

typedef struct dt_iop_splittoning_gui_data_t
{
  GtkWidget *balance_scale, *compress_scale;
  GtkWidget *shadow_colorpick, *highlight_colorpick;
  GtkWidget *shadow_hue_gslider, *shadow_sat_gslider;
  GtkWidget *highlight_hue_gslider, *highlight_sat_gslider;
  dt_iop_color_picker_t color_picker;
} dt_iop_splittoning_gui_data_t;

typedef struct dt_iop_splittoning_data_t
{
  float shadow_hue;
  float shadow_saturation;
  float highlight_hue;
  float highlight_saturation;
  float balance;  // center luminance of gradient}
  float compress; // Compress range
} dt_iop_splittoning_data_t;

typedef struct dt_iop_splittoning_global_data_t
{
  int kernel_splittoning;
} dt_iop_splittoning_global_data_t;

typedef enum dt_iop_splittoning_picker_t
{
  DT_SPLITTONING_NONE = 0,
  DT_SPLITTONING_HIGHLIGHTS,
  DT_SPLITTONING_SHADOWS
} dt_iop_splittoning_picker_data_t;


const char *name()
{
  return _("split-toning");
}

int flags()
{
  return IOP_FLAGS_INCLUDE_IN_STYLES | IOP_FLAGS_SUPPORTS_BLENDING | IOP_FLAGS_ALLOW_TILING;
}

int default_group()
{
  return IOP_GROUP_EFFECT;
}

int default_colorspace(dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  return iop_cs_rgb;
}

void init_key_accels(dt_iop_module_so_t *self)
{
  dt_accel_register_iop(self, FALSE, NC_("accel", "pick primary color"), 0, 0);
  dt_accel_register_iop(self, FALSE, NC_("accel", "pick secondary color"), 0, 0);

  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "shadows-hue"));
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "shadows-saturation"));
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "highlights-hue"));
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "highlights-saturation"));
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "balance"));
  dt_accel_register_slider_iop(self, FALSE, NC_("accel", "compress"));
}

void connect_key_accels(dt_iop_module_t *self)
{
  dt_iop_splittoning_gui_data_t *g = (dt_iop_splittoning_gui_data_t *)self->gui_data;

  dt_accel_connect_button_iop(self, "pick primary color", GTK_WIDGET(g->shadow_colorpick));
  dt_accel_connect_button_iop(self, "pick secondary color", GTK_WIDGET(g->highlight_colorpick));

  dt_accel_connect_slider_iop(self, "shadows-hue", GTK_WIDGET(g->shadow_hue_gslider));
  dt_accel_connect_slider_iop(self, "shadows-saturation", GTK_WIDGET(g->shadow_sat_gslider));
  dt_accel_connect_slider_iop(self, "highlights-hue", GTK_WIDGET(g->highlight_hue_gslider));
  dt_accel_connect_slider_iop(self, "highlights-saturation", GTK_WIDGET(g->highlight_sat_gslider));
  dt_accel_connect_slider_iop(self, "balance", GTK_WIDGET(g->balance_scale));
  dt_accel_connect_slider_iop(self, "compress", GTK_WIDGET(g->compress_scale));
}

void init_presets(dt_iop_module_so_t *self)
{
  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), "BEGIN", NULL, NULL, NULL);

  // shadows: #ED7212
  // highlights: #ECA413
  // balance : 63
  // compress : 0
  dt_gui_presets_add_generic(
      _("authentic sepia"), self->op, self->version(),
      &(dt_iop_splittoning_params_t){ 26.0 / 360.0, 92.0 / 100.0, 40.0 / 360.0, 92.0 / 100.0, 0.63, 0.0 },
      sizeof(dt_iop_splittoning_params_t), 1);

  // shadows: #446CBB
  // highlights: #446CBB
  // balance : 0
  // compress : 5.22
  dt_gui_presets_add_generic(
      _("authentic cyanotype"), self->op, self->version(),
      &(dt_iop_splittoning_params_t){ 220.0 / 360.0, 64.0 / 100.0, 220.0 / 360.0, 64.0 / 100.0, 0.0, 5.22 },
      sizeof(dt_iop_splittoning_params_t), 1);

  // shadows : #A16C5E
  // highlights : #A16C5E
  // balance : 100
  // compress : 0
  dt_gui_presets_add_generic(
      _("authentic platinotype"), self->op, self->version(),
      &(dt_iop_splittoning_params_t){ 13.0 / 360.0, 42.0 / 100.0, 13.0 / 360.0, 42.0 / 100.0, 100.0, 0.0 },
      sizeof(dt_iop_splittoning_params_t), 1);

  // shadows: #211A14
  // highlights: #D9D0C7
  // balance : 60
  // compress : 0
  dt_gui_presets_add_generic(
      _("chocolate brown"), self->op, self->version(),
      &(dt_iop_splittoning_params_t){ 28.0 / 360.0, 39.0 / 100.0, 28.0 / 360.0, 8.0 / 100.0, 0.60, 0.0 },
      sizeof(dt_iop_splittoning_params_t), 1);

  DT_DEBUG_SQLITE3_EXEC(dt_database_get(darktable.db), "COMMIT", NULL, NULL, NULL);
}

void process(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const void *const ivoid,
             void *const ovoid, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  dt_iop_splittoning_data_t *data = (dt_iop_splittoning_data_t *)piece->data;
  float *in;
  float *out;
  const int ch = piece->colors;

  const float compress = (data->compress / 110.0) / 2.0; // Don't allow 100% compression..
#ifdef _OPENMP
#pragma omp parallel for default(none) \
  dt_omp_firstprivate(ch, compress, ivoid, ovoid, roi_out) \
  shared(data) \
  private(in, out) \
  schedule(static)
#endif
  for(int k = 0; k < roi_out->height; k++)
  {
    in = ((float *)ivoid) + (size_t)ch * k * roi_out->width;
    out = ((float *)ovoid) + (size_t)ch * k * roi_out->width;
    for(int j = 0; j < roi_out->width; j++, in += ch, out += ch)
    {
      double ra, la;
      float mixrgb[3];
      float h, s, l;
      rgb2hsl(in, &h, &s, &l);
      if(l < data->balance - compress || l > data->balance + compress)
      {
        h = l < data->balance ? data->shadow_hue : data->highlight_hue;
        s = l < data->balance ? data->shadow_saturation : data->highlight_saturation;
        ra = l < data->balance ? CLIP((fabs(-data->balance + compress + l) * 2.0))
                               : CLIP((fabs(-data->balance - compress + l) * 2.0));
        la = (1.0 - ra);

        hsl2rgb(mixrgb, h, s, l);

        out[0] = CLIP(in[0] * la + mixrgb[0] * ra);
        out[1] = CLIP(in[1] * la + mixrgb[1] * ra);
        out[2] = CLIP(in[2] * la + mixrgb[2] * ra);
      }
      else
      {
        out[0] = in[0];
        out[1] = in[1];
        out[2] = in[2];
      }

      out[3] = in[3];
    }
  }
}

#ifdef HAVE_OPENCL
int process_cl(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, cl_mem dev_in, cl_mem dev_out,
               const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  dt_iop_splittoning_data_t *d = (dt_iop_splittoning_data_t *)piece->data;
  dt_iop_splittoning_global_data_t *gd = (dt_iop_splittoning_global_data_t *)self->global_data;

  cl_int err = -999;
  const int devid = piece->pipe->devid;

  const int width = roi_out->width;
  const int height = roi_out->height;

  const float compress = (d->compress / 110.0) / 2.0; // Don't allow 100% compression..
  const float balance = d->balance;
  const float shadow_hue = d->shadow_hue;
  const float shadow_saturation = d->shadow_saturation;
  const float highlight_hue = d->highlight_hue;
  const float highlight_saturation = d->highlight_saturation;

  size_t sizes[2] = { ROUNDUPWD(width), ROUNDUPHT(height) };
  dt_opencl_set_kernel_arg(devid, gd->kernel_splittoning, 0, sizeof(cl_mem), &dev_in);
  dt_opencl_set_kernel_arg(devid, gd->kernel_splittoning, 1, sizeof(cl_mem), &dev_out);
  dt_opencl_set_kernel_arg(devid, gd->kernel_splittoning, 2, sizeof(int), &width);
  dt_opencl_set_kernel_arg(devid, gd->kernel_splittoning, 3, sizeof(int), &height);
  dt_opencl_set_kernel_arg(devid, gd->kernel_splittoning, 4, sizeof(float), &compress);
  dt_opencl_set_kernel_arg(devid, gd->kernel_splittoning, 5, sizeof(float), &balance);
  dt_opencl_set_kernel_arg(devid, gd->kernel_splittoning, 6, sizeof(float), &shadow_hue);
  dt_opencl_set_kernel_arg(devid, gd->kernel_splittoning, 7, sizeof(float), &shadow_saturation);
  dt_opencl_set_kernel_arg(devid, gd->kernel_splittoning, 8, sizeof(float), &highlight_hue);
  dt_opencl_set_kernel_arg(devid, gd->kernel_splittoning, 9, sizeof(float), &highlight_saturation);
  err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_splittoning, sizes);
  if(err != CL_SUCCESS) goto error;
  return TRUE;

error:
  dt_print(DT_DEBUG_OPENCL, "[opencl_splittoning] couldn't enqueue kernel! %d\n", err);
  return FALSE;
}
#endif


void init_global(dt_iop_module_so_t *module)
{
  const int program = 8; // extended.cl from programs.conf
  dt_iop_splittoning_global_data_t *gd
      = (dt_iop_splittoning_global_data_t *)malloc(sizeof(dt_iop_splittoning_global_data_t));
  module->data = gd;
  gd->kernel_splittoning = dt_opencl_create_kernel(program, "splittoning");
}


void cleanup_global(dt_iop_module_so_t *module)
{
  dt_iop_splittoning_global_data_t *gd = (dt_iop_splittoning_global_data_t *)module->data;
  dt_opencl_free_kernel(gd->kernel_splittoning);
  free(module->data);
  module->data = NULL;
}


static void balance_callback(GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_splittoning_params_t *p = (dt_iop_splittoning_params_t *)self->params;
  p->balance = 1.0f - dt_bauhaus_slider_get(slider) / 100.0f;
  dt_iop_color_picker_reset(self, TRUE);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void compress_callback(GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  if(self->dt->gui->reset) return;
  dt_iop_splittoning_params_t *p = (dt_iop_splittoning_params_t *)self->params;
  p->compress = dt_bauhaus_slider_get(slider);
  dt_iop_color_picker_reset(self, TRUE);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static inline void update_colorpicker_color(GtkWidget *colorpicker, float hue, float sat)
{
  float rgb[3];
  hsl2rgb(rgb, hue, sat, 0.5);

  GdkRGBA color = (GdkRGBA){.red = rgb[0], .green = rgb[1], .blue = rgb[2], .alpha = 1.0 };
  gtk_color_chooser_set_rgba(GTK_COLOR_CHOOSER(colorpicker), &color);
}

static inline void update_saturation_slider_end_color(GtkWidget *slider, float hue)
{
  float rgb[3];
  hsl2rgb(rgb, hue, 1.0, 0.5);
  dt_bauhaus_slider_set_stop(slider, 1.0, rgb[0], rgb[1], rgb[2]);
}

static inline void update_balance_slider_colors(
    GtkWidget *slider,
    float shadow_hue,
    float highlight_hue)
{
  float rgb[3];
  if(shadow_hue != -1)
  {
    hsl2rgb(rgb, shadow_hue, 1.0, 0.5);
    dt_bauhaus_slider_set_stop(slider, 0.0, rgb[0], rgb[1], rgb[2]);
  }
  if(highlight_hue != -1)
  {
    hsl2rgb(rgb, highlight_hue, 1.0, 0.5);
    dt_bauhaus_slider_set_stop(slider, 1.0, rgb[0], rgb[1], rgb[2]);
  }
}

static void hue_callback(GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_splittoning_params_t *p = (dt_iop_splittoning_params_t *)self->params;
  dt_iop_splittoning_gui_data_t *g = (dt_iop_splittoning_gui_data_t *)self->gui_data;

  dt_iop_color_picker_reset(self, TRUE);

  double hue = 0;
  double saturation = 0;
  GtkWidget *colorpicker;
  GtkWidget *sat_slider = NULL;
  if(slider == g->shadow_hue_gslider)
  {
    // Shadows
    hue = p->shadow_hue = dt_bauhaus_slider_get(slider);
    saturation = p->shadow_saturation;
    colorpicker = GTK_WIDGET(g->shadow_colorpick);
    sat_slider = g->shadow_sat_gslider;
    update_balance_slider_colors(g->balance_scale, hue, -1);
  }
  else
  {
    hue = p->highlight_hue = dt_bauhaus_slider_get(slider);
    saturation = p->highlight_saturation;
    colorpicker = GTK_WIDGET(g->highlight_colorpick);
    sat_slider = g->highlight_sat_gslider;
    update_balance_slider_colors(g->balance_scale, -1, hue);
  }

  update_colorpicker_color(colorpicker, hue, saturation);
  update_saturation_slider_end_color(sat_slider, hue);

  if(self->dt->gui->reset) return;

  gtk_widget_queue_draw(GTK_WIDGET(sat_slider));
  gtk_widget_queue_draw(GTK_WIDGET(g->balance_scale));

  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void saturation_callback(GtkWidget *slider, gpointer user_data)
{
  dt_iop_module_t *self = (dt_iop_module_t *)user_data;
  dt_iop_splittoning_params_t *p = (dt_iop_splittoning_params_t *)self->params;
  dt_iop_splittoning_gui_data_t *g = (dt_iop_splittoning_gui_data_t *)self->gui_data;

  dt_iop_color_picker_reset(self, TRUE);

  double hue = 0;
  double saturation = 0;
  GtkWidget *colorpicker;
  if(slider == g->shadow_sat_gslider)
  {
    // Shadows
    hue = dt_bauhaus_slider_get(g->shadow_hue_gslider);
    p->shadow_saturation = saturation = dt_bauhaus_slider_get(slider);
    colorpicker = GTK_WIDGET(g->shadow_colorpick);
  }
  else
  {
    hue = dt_bauhaus_slider_get(g->highlight_hue_gslider);
    p->highlight_saturation = saturation = dt_bauhaus_slider_get(slider);
    colorpicker = GTK_WIDGET(g->highlight_colorpick);
  }

  update_colorpicker_color(colorpicker, hue, saturation);

  if(self->dt->gui->reset) return;
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void colorpick_callback(GtkColorButton *widget, dt_iop_module_t *self)
{
  if(self->dt->gui->reset) return;

  dt_iop_splittoning_gui_data_t *g = (dt_iop_splittoning_gui_data_t *)self->gui_data;

  float color[3], h, s, l;

  GdkRGBA c;
  gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(widget), &c);
  color[0] = c.red;
  color[1] = c.green;
  color[2] = c.blue;
  rgb2hsl(color, &h, &s, &l);

  if (GTK_WIDGET(widget) == g->shadow_colorpick)
  {
      dt_bauhaus_slider_set(g->shadow_hue_gslider, h);
      dt_bauhaus_slider_set(g->shadow_sat_gslider, s);
      update_balance_slider_colors(g->balance_scale, h, -1);
  }
  else
  {
      dt_bauhaus_slider_set(g->highlight_hue_gslider, h);
      dt_bauhaus_slider_set(g->highlight_sat_gslider, s);
      update_balance_slider_colors(g->balance_scale, -1,  h);
  }

  gtk_widget_queue_draw(GTK_WIDGET(g->balance_scale));

  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static int _iop_color_picker_get_set(dt_iop_module_t *self, GtkWidget *button)
{
  dt_iop_splittoning_gui_data_t *g =  (dt_iop_splittoning_gui_data_t *)self->gui_data;

  const int current_picker = g->color_picker.current_picker;

  g->color_picker.current_picker = DT_SPLITTONING_NONE;

  if(button == g->shadow_hue_gslider)
    g->color_picker.current_picker = DT_SPLITTONING_SHADOWS;
  else if(button == g->highlight_hue_gslider)
    g->color_picker.current_picker = DT_SPLITTONING_HIGHLIGHTS;

  if (current_picker == g->color_picker.current_picker)
    return DT_COLOR_PICKER_ALREADY_SELECTED;
  else
    return g->color_picker.current_picker;
}

static void _iop_color_picker_apply(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_splittoning_gui_data_t *g = (dt_iop_splittoning_gui_data_t *)self->gui_data;
  dt_iop_splittoning_params_t *p = (dt_iop_splittoning_params_t *)self->params;

  float *p_hue, *p_saturation;
  GtkWidget *sat, *hue, *colorpicker;

  // convert picker RGB 2 HSL
  float H = .0f, S = .0f, L = .0f;
  rgb2hsl(self->picked_color, &H, &S, &L);

  if(g->color_picker.current_picker == DT_SPLITTONING_HIGHLIGHTS)
  {
    p_hue = &p->highlight_hue;
    p_saturation = &p->highlight_saturation;
    hue = g->highlight_hue_gslider;
    sat = g->highlight_sat_gslider;
    colorpicker = g->highlight_colorpick;
    update_balance_slider_colors(g->balance_scale, -1, H);
  }
  else
  {
    p_hue = &p->shadow_hue;
    p_saturation = &p->shadow_saturation;
    hue = g->shadow_hue_gslider;
    sat = g->shadow_sat_gslider;
    colorpicker = g->shadow_colorpick;
    update_balance_slider_colors(g->balance_scale, H, -1);
  }

  if(fabsf(*p_hue - H) < 0.0001f && fabsf(*p_saturation - S) < 0.0001f)
  {
    // interrupt infinite loops
    return;
  }

  *p_hue        = H;
  *p_saturation = S;

  const int reset = darktable.gui->reset;
  darktable.gui->reset = 1;
  dt_bauhaus_slider_set(hue, H);
  dt_bauhaus_slider_set(sat, S);
  update_colorpicker_color(colorpicker, H, S);
  update_saturation_slider_end_color(sat, H);
  darktable.gui->reset = reset;

  gtk_widget_queue_draw(GTK_WIDGET(g->balance_scale));

  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void _iop_color_picker_update(dt_iop_module_t *self)
{
  dt_iop_splittoning_gui_data_t *g =  (dt_iop_splittoning_gui_data_t *)self->gui_data;
  dt_bauhaus_widget_set_quad_active(g->shadow_hue_gslider, g->color_picker.current_picker == DT_SPLITTONING_SHADOWS);
  dt_bauhaus_widget_set_quad_active(g->highlight_hue_gslider, g->color_picker.current_picker == DT_SPLITTONING_HIGHLIGHTS);
}

void commit_params(struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe,
                   dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_splittoning_params_t *p = (dt_iop_splittoning_params_t *)p1;
  dt_iop_splittoning_data_t *d = (dt_iop_splittoning_data_t *)piece->data;

  d->shadow_hue = p->shadow_hue;
  d->highlight_hue = p->highlight_hue;
  d->shadow_saturation = p->shadow_saturation;
  d->highlight_saturation = p->highlight_saturation;
  d->balance = p->balance;
  d->compress = p->compress;
}

void init_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = calloc(1, sizeof(dt_iop_splittoning_data_t));
  self->commit_params(self, self->default_params, pipe, piece);
}

void cleanup_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  free(piece->data);
  piece->data = NULL;
}

void gui_update(struct dt_iop_module_t *self)
{
  dt_iop_module_t *module = (dt_iop_module_t *)self;
  dt_iop_splittoning_gui_data_t *g = (dt_iop_splittoning_gui_data_t *)self->gui_data;
  dt_iop_splittoning_params_t *p = (dt_iop_splittoning_params_t *)module->params;

  dt_bauhaus_slider_set(g->shadow_hue_gslider, p->shadow_hue);
  dt_bauhaus_slider_set(g->shadow_sat_gslider, p->shadow_saturation);
  dt_bauhaus_slider_set(g->highlight_hue_gslider, p->highlight_hue);
  dt_bauhaus_slider_set(g->highlight_sat_gslider, p->highlight_saturation);
  dt_bauhaus_slider_set(g->balance_scale, (1 - p->balance) * 100.0);
  dt_bauhaus_slider_set(g->compress_scale, p->compress);

  update_colorpicker_color(GTK_WIDGET(g->shadow_colorpick), p->shadow_hue, p->shadow_saturation);
  update_colorpicker_color(GTK_WIDGET(g->highlight_colorpick), p->highlight_hue, p->highlight_saturation);
  update_saturation_slider_end_color(g->shadow_sat_gslider, p->shadow_hue);
  update_saturation_slider_end_color(g->highlight_sat_gslider, p->highlight_hue);

  update_balance_slider_colors(g->balance_scale, p->shadow_hue, p->highlight_hue);
}

void init(dt_iop_module_t *module)
{
  module->params = calloc(1, sizeof(dt_iop_splittoning_params_t));
  module->default_params = calloc(1, sizeof(dt_iop_splittoning_params_t));
  module->default_enabled = 0;
  module->params_size = sizeof(dt_iop_splittoning_params_t);
  module->gui_data = NULL;
  dt_iop_splittoning_params_t tmp = (dt_iop_splittoning_params_t){ 0, 0.5, 0.2, 0.5, 0.5, 33.0 };
  memcpy(module->params, &tmp, sizeof(dt_iop_splittoning_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_splittoning_params_t));
}

void cleanup(dt_iop_module_t *module)
{
  free(module->params);
  module->params = NULL;
  free(module->default_params);
  module->default_params = NULL;
}

static inline int gui_init_tab(
    struct dt_iop_module_t *self,
    int line,
    const char *name,
    GtkWidget **ppcolor,
    const GdkRGBA *c,
    GtkWidget **pphue,
    GtkWidget **ppsaturation)
{
  dt_iop_splittoning_gui_data_t *g = (dt_iop_splittoning_gui_data_t *)self->gui_data;

  GtkGrid *grid = GTK_GRID(self->widget);
  gtk_grid_attach(grid, dt_ui_section_label_new(name), 0, line++, 2, 1);

  // color button
  GtkWidget *color;
  *ppcolor = color = gtk_color_button_new_with_rgba(c);
  gtk_color_chooser_set_use_alpha(GTK_COLOR_CHOOSER(color), FALSE);
  gtk_color_button_set_title(GTK_COLOR_BUTTON(color), _("select tone color"));
  g_signal_connect(G_OBJECT(color), "color-set", G_CALLBACK(colorpick_callback), self);

  // hue slider
  GtkWidget *hue;
  *pphue = hue = (dt_bauhaus_slider_new_with_range_and_feedback(self, 0.0f, 1.0f, 0.01f, 0.0f, 2, 0));
  dt_bauhaus_slider_set_stop(hue, 0.0f, 1.0f, 0.0f, 0.0f);
  dt_bauhaus_widget_set_label(hue, NULL, _("hue"));
  dt_bauhaus_slider_set_stop(hue, 0.166f, 1.0f, 1.0f, 0.0f);
  dt_bauhaus_slider_set_stop(hue, 0.322f, 0.0f, 1.0f, 0.0f);
  dt_bauhaus_slider_set_stop(hue, 0.498f, 0.0f, 1.0f, 1.0f);
  dt_bauhaus_slider_set_stop(hue, 0.664f, 0.0f, 0.0f, 1.0f);
  dt_bauhaus_slider_set_stop(hue, 0.830f, 1.0f, 0.0f, 1.0f);
  dt_bauhaus_slider_set_stop(hue, 1.0f, 1.0f, 0.0f, 0.0f);
  gtk_widget_set_tooltip_text(hue, _("select the hue tone"));
  dt_bauhaus_widget_set_quad_paint(hue, dtgtk_cairo_paint_colorpicker, CPF_STYLE_FLAT | CPF_DO_NOT_USE_BORDER, NULL);
  dt_bauhaus_widget_set_quad_toggle(hue, TRUE);
  g_signal_connect(G_OBJECT(hue), "quad-pressed", G_CALLBACK(dt_iop_color_picker_callback), &g->color_picker);
  g_signal_connect(G_OBJECT(hue), "value-changed", G_CALLBACK(hue_callback), self);

  // saturation slider
  GtkWidget *saturation;
  *ppsaturation = saturation = dt_bauhaus_slider_new_with_range(self, 0.0f, 1.0f, 0.01f, 0.0f, 2);
  dt_bauhaus_widget_set_label(saturation, NULL, _("saturation"));
  dt_bauhaus_slider_set_stop(saturation, 0.0f, 0.2f, 0.2f, 0.2f);
  dt_bauhaus_slider_set_stop(saturation, 1.0f, 1.0f, 1.0f, 1.0f);
  gtk_widget_set_tooltip_text(saturation, _("select the saturation tone"));
  g_signal_connect(G_OBJECT(saturation), "value-changed", G_CALLBACK(saturation_callback), self);

  // pack the widgets
  gtk_widget_set_hexpand(hue, TRUE); // make sure that the color picker doesn't become HUGE
  gtk_grid_attach(grid, hue, 0, line, 1, 1);
  gtk_grid_attach(grid, color, 1, line++, 1, 2);
  gtk_grid_attach(grid, saturation, 0, line++, 1, 1);

  return line;
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_splittoning_gui_data_t));
  dt_iop_splittoning_gui_data_t *g = (dt_iop_splittoning_gui_data_t *)self->gui_data;
  dt_iop_splittoning_params_t *p = (dt_iop_splittoning_params_t *)self->params;

  int line = 0;
  self->widget = gtk_grid_new();
  GtkGrid *grid = GTK_GRID(self->widget);
  gtk_grid_set_row_spacing(grid, DT_BAUHAUS_SPACE);
  gtk_grid_set_column_spacing(grid, DT_BAUHAUS_SPACE);
  gtk_grid_set_column_homogeneous(grid, FALSE);
  dt_gui_add_help_link(self->widget, dt_get_help_url(self->op));

  float rgb[3];

  // Shadows
  hsl2rgb(rgb, p->shadow_hue, p->shadow_saturation, 0.5f);
  GdkRGBA sh_color = (GdkRGBA){.red = rgb[0], .green = rgb[1], .blue = rgb[2], .alpha = 1.0 };
  line = gui_init_tab(self, line, _("shadows"), &g->shadow_colorpick, &sh_color, &g->shadow_hue_gslider, &g->shadow_sat_gslider);

  // Highlights
  hsl2rgb(rgb, p->highlight_hue, p->highlight_saturation, 0.5f);
  GdkRGBA hi_color = (GdkRGBA){.red = rgb[0], .green = rgb[1], .blue = rgb[2], .alpha = 1.0 };
  line = gui_init_tab(self, line, _("highlights"), &g->highlight_colorpick, &hi_color, &g->highlight_hue_gslider, &g->highlight_sat_gslider);

  // Additional parameters

  gtk_grid_attach(grid, dt_ui_section_label_new(_("properties")), 0, line++, 2, 1);

  g->balance_scale = dt_bauhaus_slider_new_with_range_and_feedback(
      self, 0.0, 100.0, 0.1, (1- p->balance) * 100.0, 2, 0);
  dt_bauhaus_slider_set_format(g->balance_scale, "%.2f");
  dt_bauhaus_slider_set_stop(g->balance_scale, 0.0f, 0.5f, 0.5f, 0.5f);
  dt_bauhaus_slider_set_stop(g->balance_scale, 1.0f, 0.5f, 0.5f, 0.5f);
  dt_bauhaus_widget_set_label(g->balance_scale, NULL, _("balance"));
  gtk_widget_set_tooltip_text(g->balance_scale, _("the balance of center of split-toning"));
  g_signal_connect(G_OBJECT(g->balance_scale), "value-changed", G_CALLBACK(balance_callback), self);
  gtk_grid_attach(grid, g->balance_scale, 0, line++, 2, 1);

  g->compress_scale = dt_bauhaus_slider_new_with_range(self, 0.0, 100.0, 1.0, p->compress, 2);
  dt_bauhaus_slider_set_format(g->compress_scale, "%.2f%%");
  dt_bauhaus_widget_set_label(g->compress_scale, NULL, _("compress"));
  gtk_grid_attach(grid, g->compress_scale, 0, line++, 2, 1);
  gtk_widget_set_tooltip_text(g->compress_scale, _("compress the effect on highlights/shadows and\npreserve midtones"));
  g_signal_connect(G_OBJECT(g->compress_scale), "value-changed", G_CALLBACK(compress_callback), self);

  dt_iop_init_picker(&g->color_picker,
              self,
              DT_COLOR_PICKER_POINT,
              _iop_color_picker_get_set,
              _iop_color_picker_apply,
              _iop_color_picker_update);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  free(self->gui_data);
  self->gui_data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
