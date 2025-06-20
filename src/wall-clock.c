/*
 * Copyright (C) 2024 The Phosh Developers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "wall-clock-priv.h"

#define GNOME_DESKTOP_USE_UNSTABLE_API
#include <libgnome-desktop/gnome-wall-clock.h>

#include <glib/gi18n.h>
#include <locale.h>

enum {
  PROP_0,
  PROP_DATE_TIME,
  PROP_TIME,
  LAST_PROP,
};
static GParamSpec *props[LAST_PROP];

/**
 * PhoshWallClock:
 *
 * Wall clock used for fetching date and time
 */

typedef struct _PhoshWallClockPrivate {
  GnomeWallClock *time;
  GnomeWallClock *date_time;

  GRegex         *clock_re;
} PhoshWallClockPrivate;

G_DEFINE_TYPE_WITH_PRIVATE (PhoshWallClock, phosh_wall_clock, G_TYPE_OBJECT)


static void
on_date_time_changed (PhoshWallClock *self, GParamSpec *pspec, GnomeWallClock *clock)
{
  g_return_if_fail (PHOSH_IS_WALL_CLOCK (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_DATE_TIME]);
}


static void
on_time_changed (PhoshWallClock *self, GParamSpec *pspec, GnomeWallClock *clock)
{
  g_return_if_fail (PHOSH_IS_WALL_CLOCK (self));
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_TIME]);
}


static void
phosh_wall_clock_get_property (GObject    *object,
                               guint       property_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  PhoshWallClock *self = PHOSH_WALL_CLOCK (object);

  switch (property_id) {
  case PROP_DATE_TIME:
    g_value_set_string (value, phosh_wall_clock_get_clock (self, FALSE));
    break;
  case PROP_TIME:
    g_value_set_string (value, phosh_wall_clock_get_clock (self, TRUE));
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    break;
  }
}


static void
phosh_wall_clock_dispose (GObject *object)
{
  PhoshWallClock *self = PHOSH_WALL_CLOCK (object);
  PhoshWallClockPrivate *priv = phosh_wall_clock_get_instance_private (self);

  g_clear_object (&priv->date_time);
  g_clear_object (&priv->time);
  g_clear_pointer (&priv->clock_re, g_regex_unref);

  G_OBJECT_CLASS (phosh_wall_clock_parent_class)->dispose (object);
}


static const char *
get_clock_impl (PhoshWallClock *self, gboolean time_only)
{
  PhoshWallClockPrivate *priv = phosh_wall_clock_get_instance_private (self);

  return gnome_wall_clock_get_clock (time_only ? priv->time : priv->date_time);
}


static gint64
get_time_t_impl (PhoshWallClock *self)
{
  return time (NULL);
}


static void
phosh_wall_clock_class_init (PhoshWallClockClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = phosh_wall_clock_dispose;
  object_class->get_property = phosh_wall_clock_get_property;

  klass->get_clock = get_clock_impl;
  klass->get_time_t = get_time_t_impl;

  /**
   * PhoshWallClock::date-time:
   *
   * The current date and time
   */
  props[PROP_DATE_TIME] =
    g_param_spec_string ("date-time", "", "",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);
  /**
   * PhoshWallClock::time:
   *
   * The current time
   */
  props[PROP_TIME] =
    g_param_spec_string ("time", "", "",
                         NULL,
                         G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);
}


static void
phosh_wall_clock_init (PhoshWallClock *self)
{
  PhoshWallClockPrivate *priv = phosh_wall_clock_get_instance_private (self);

  priv->date_time = gnome_wall_clock_new ();
  priv->time = g_object_new (GNOME_TYPE_WALL_CLOCK, "time-only", TRUE, NULL);
  priv->clock_re = g_regex_new ("[0-9]{1,2}.+[0-9]{1,2}(.+[0-9]{1,2})?",
                                G_REGEX_DEFAULT | G_REGEX_OPTIMIZE,
                                G_REGEX_MATCH_DEFAULT,
                                NULL);
  g_assert (priv->clock_re);
  /* Somewhat icky, we need a distinct handler for each clock because one can
   * update before the other, and we don't know which one the caller wants to
   * read from.
   */
  g_signal_connect_object (priv->date_time, "notify::clock", G_CALLBACK (on_date_time_changed),
                           self, G_CONNECT_SWAPPED);
  g_signal_connect_object (priv->time, "notify::clock", G_CALLBACK (on_time_changed),
                           self, G_CONNECT_SWAPPED);
}


PhoshWallClock *
phosh_wall_clock_new (void)
{
  return g_object_new (PHOSH_TYPE_WALL_CLOCK, NULL);
}

static PhoshWallClock *instance;

/**
 * phosh_wall_clock_set_default:
 * @self: The clock to use
 *
 * Set the wall clock singleton. This sets the singleton returned by
 * `phosh_wall_clock_get_default()`.
 */
void
phosh_wall_clock_set_default (PhoshWallClock *self)
{
  g_return_if_fail (PHOSH_IS_WALL_CLOCK (self));

  g_clear_object (&instance);

  instance = self;
  g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *)&instance);
}

/**
 * phosh_wall_clock_get_default:
 *
 * Get the wall clock singleton
 *
 * Returns:(transfer none): The wall clock singleton
 */
PhoshWallClock *
phosh_wall_clock_get_default (void)
{
  if (!instance)
    g_error ("Wall clock singleton not set");

  return instance;
}

/**
 * phosh_wall_clock_get_clock:
 * @self: The wall clock
 * @time_only: whether to return full clock string or just the time
 *
 * Gets the current clock string, if time_only is true this will be just the
 * current time, otherwise the date + time.
 *
 * Returns: the clock time string
 */
const char *
phosh_wall_clock_get_clock (PhoshWallClock *self, gboolean time_only)
{
  PhoshWallClockClass *klass;

  g_return_val_if_fail (PHOSH_IS_WALL_CLOCK (self), "");

  klass = PHOSH_WALL_CLOCK_GET_CLASS (self);

  return klass->get_clock (self, time_only);
}

/**
 * phosh_wall_clock_date_fmt:
 *
 * Get a date format based on LC_TIME.
 * This is done by temporarily switching LC_MESSAGES so we can look up
 * the format in our message catalog.  This will fail if LANGUAGE is
 * set to something different since LANGUAGE overrides
 * LC_{ALL,MESSAGE}.
 */
static const char *
phosh_wall_clock_date_fmt (void)
{
  const char *locale;
  const char *fmt;

  locale = setlocale (LC_TIME, NULL);
  if (locale) /* Lookup date format via messages catalog */
    setlocale (LC_MESSAGES, locale);
  /* Translators: This is a time format for a date in
     long format */
  fmt = _("%A, %B %-e");
  setlocale (LC_MESSAGES, "");
  return fmt;
}

/**
 * phosh_wall_clock_local_date:
 * @self: The wall clock
 *
 * Get the local date as string
 * We honor LC_MESSAGES so we e.g. don't get a translated date when
 * the user has LC_MESSAGES=en_US.UTF-8 but LC_TIME to their local
 * time zone.
 *
 * Returns: The local date as string
 */
char *
phosh_wall_clock_local_date (PhoshWallClock *self)
{
  PhoshWallClockClass *klass;
  time_t current;
  struct tm local;
  g_autofree char *date = NULL;
  const char *fmt;
  const char *locale;

  g_return_val_if_fail (PHOSH_IS_WALL_CLOCK (self), NULL);

  klass = PHOSH_WALL_CLOCK_GET_CLASS (self);
  current = klass->get_time_t (self);

  g_return_val_if_fail (current != (time_t) -1, NULL);
  g_return_val_if_fail (localtime_r (&current, &local), NULL);

  date = g_malloc0 (256);
  fmt = phosh_wall_clock_date_fmt ();
  locale = setlocale (LC_MESSAGES, NULL);
  if (locale) /* make sure weekday and month use LC_MESSAGES */
    setlocale (LC_TIME, locale);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
  /* Can't use a string literal since it needs to be translated */
  g_return_val_if_fail (strftime (date, 255, fmt, &local), NULL);
#pragma GCC diagnostic pop
  setlocale (LC_TIME, "");
  return g_steal_pointer (&date);
}


char *
phosh_wall_clock_string_for_datetime (PhoshWallClock      *self,
                                      GDateTime           *datetime,
                                      GDesktopClockFormat  clock_format,
                                      gboolean             show_full_date)
{
  PhoshWallClockPrivate *priv;

  g_return_val_if_fail (PHOSH_IS_WALL_CLOCK (self), NULL);
  priv = phosh_wall_clock_get_instance_private (self);

  return gnome_wall_clock_string_for_datetime (priv->time, datetime, clock_format,
                                               FALSE, show_full_date, FALSE);
}

/**
 * phosh_wall_clock_strip_am_pm:
 * @time: the time string
 *
 * Strip " {A,P}M" from 12h time format to look less cramped. Note
 * that this also strips any surrounding white space so " 1:00 AM"
 * becomes "1:00".
 */
char *
phosh_wall_clock_strip_am_pm (PhoshWallClock *self, const char *time)
{
  PhoshWallClockPrivate *priv;
  g_auto (GStrv) parts = NULL;

  g_return_val_if_fail (PHOSH_IS_WALL_CLOCK (self), NULL);
  priv = phosh_wall_clock_get_instance_private (self);

  parts = g_strsplit (time, " ", -1);
  if (g_strv_length (parts) == 1)
    return g_strdup (parts[0]);

  if (g_strv_length (parts) != 2) {
    g_warning ("Can't parse time format: %s", time);
    return g_strdup (time);
  }

  for (int i = 0; parts[i]; i++) {
    g_autoptr (GMatchInfo) info = NULL;

    if (g_regex_match (priv->clock_re, parts[i], G_REGEX_MATCH_DEFAULT, &info))
      return g_match_info_fetch (info, 0);
  }

  g_warning ("Can't match time format: %s", time);
  return g_strdup (time);
}
