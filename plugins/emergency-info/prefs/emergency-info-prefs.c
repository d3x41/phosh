/*
 * Copyright (C) 2022 Purism SPC
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Author: Chris Talbot <chris@talbothome.com>
 */

#include "emergency-info-common.h"
#include "emergency-info-prefs.h"
#include "emergency-info-prefs-row.h"

#define STR_IS_NULL_OR_EMPTY(x) ((x) == NULL || (x)[0] == '\0')

/**
 * PhoshEmergencyInfoPrefs
 *
 * Preferences for emergency-info plugin
 */
struct _PhoshEmergencyInfoPrefs {
  AdwPreferencesDialog parent;

  char                *owner_name;
  char                *dob;
  char                *language;
  char                *home_address;
  char                *age;
  char                *blood_type;
  char                *height;
  char                *weight;
  char                *allergies;
  char                *medications_conditions;
  char                *other_info;
  GStrv                contacts;

  GtkTextBuffer       *home_addr_text_buffer;
  GtkTextBuffer       *allergies_text_buffer;
  GtkTextBuffer       *med_cond_text_buffer;
  GtkTextBuffer       *other_info_text_buffer;

  AdwEntryRow         *owner_name_entry;
  AdwEntryRow         *dob_entry;
  AdwEntryRow         *pref_language_entry;

  AdwEntryRow         *age_entry;
  AdwEntryRow         *blood_type_entry;
  AdwEntryRow         *height_entry;
  AdwEntryRow         *weight_entry;

  AdwEntryRow         *contact_name_entry;
  AdwEntryRow         *relationship_entry;
  AdwEntryRow         *contact_number_entry;

  AdwDialog           *add_emer_contact_dialog;

  AdwPreferencesGroup *emer_contacts;

  char                *keyfile_path;
};

G_DEFINE_TYPE (PhoshEmergencyInfoPrefs, phosh_emergency_info_prefs, ADW_TYPE_PREFERENCES_DIALOG);


static void
save_keyfile (PhoshEmergencyInfoPrefs *self, GKeyFile *key_file)
{
  g_autoptr (GError) err = NULL;
  g_autofree char *dirname = g_path_get_dirname (self->keyfile_path);

  g_mkdir_with_parents (dirname, 0700);
  if (!g_key_file_save_to_file (key_file, self->keyfile_path, &err))
    g_warning ("Error Saving Keyfile at %s: %s", self->keyfile_path, err->message);
}


static void
set_or_remove_info_group (GKeyFile   *key_file,
                          const char *key,
                          const char *value)
{

  if (!!(value && *value))
    g_key_file_set_string (key_file,
                           INFO_GROUP,
                           key,
                           value);
  else
    g_key_file_remove_key (key_file,
                           INFO_GROUP,
                           key, NULL);

}

static void
save_settings (PhoshEmergencyInfoPrefs *self)
{
  g_autoptr (GKeyFile) key_file = g_key_file_new ();

  g_key_file_load_from_file (key_file, self->keyfile_path, G_KEY_FILE_KEEP_COMMENTS, NULL);

  set_or_remove_info_group (key_file,
                            "OwnerName",
                            self->owner_name);
  
  set_or_remove_info_group (key_file,
                            "DateOfBirth",
                            self->dob);

  set_or_remove_info_group (key_file,
                            "PreferredLanguage",
                            self->language);

  set_or_remove_info_group (key_file,
                            "HomeAddress",
                            self->home_address);


  set_or_remove_info_group (key_file,
                            "Age",
                            self->age);

  set_or_remove_info_group (key_file,
                            "BloodType",
                            self->blood_type);

  set_or_remove_info_group (key_file,
                            "Height",
                            self->height);

  set_or_remove_info_group (key_file,
                            "Weight",
                            self->weight);

  if (self->allergies && !STR_IS_NULL_OR_EMPTY (self->allergies)) {
    g_auto (GStrv) temp_allergies = NULL;
    temp_allergies = g_strsplit (self->allergies, "\n", -1);
    g_key_file_set_string_list (key_file,
                                INFO_GROUP,
                                "Allergies",
                                (const char * const*) temp_allergies,
                                g_strv_length (temp_allergies));
  } else {
    g_key_file_remove_key (key_file,
                           INFO_GROUP,
                           "Allergies", NULL);
  }

  if (self->medications_conditions && !STR_IS_NULL_OR_EMPTY (self->medications_conditions)) {
    g_auto (GStrv) temp_med_cond = NULL;
    temp_med_cond = g_strsplit (self->medications_conditions, "\n", -1);
    g_key_file_set_string_list (key_file,
                                INFO_GROUP,
                                "MedicationsAndConditions",
                                (const char * const*)temp_med_cond,
                                g_strv_length (temp_med_cond));
  } else {
    g_key_file_remove_key (key_file,
                           INFO_GROUP,
                           "MedicationsAndConditions", NULL);
  }

  set_or_remove_info_group (key_file,
                            "OtherInfo",
                            self->other_info);

  save_keyfile (self, key_file);
}

static void
add_contact_row (PhoshEmergencyInfoPrefs *self,
                 const char              *contact,
                 const char              *number)
{
  GtkWidget *new_row;
  g_auto (GStrv) number_split = NULL;

  number_split = g_strsplit (number, ";", 2);
  new_row = phosh_emergency_info_prefs_row_new (contact,
                                                number_split[0],
                                                number_split[1]);

  adw_preferences_group_add (self->emer_contacts, GTK_WIDGET (new_row));
}

static void
load_settings (PhoshEmergencyInfoPrefs *self)
{
  g_auto (GStrv) temp_med_cond = NULL;
  g_auto (GStrv) temp_allergies = NULL;
  g_autoptr (GKeyFile) key_file = g_key_file_new ();
  gsize i;

  if (!g_key_file_load_from_file (key_file, self->keyfile_path, 0, NULL))
    return;

  self->owner_name = g_key_file_get_string (key_file,
                                            INFO_GROUP,
                                            "OwnerName",
                                            NULL);

  gtk_editable_set_text (GTK_EDITABLE (self->owner_name_entry), self->owner_name ?: "");

  self->dob = g_key_file_get_string (key_file,
                                     INFO_GROUP,
                                     "DateOfBirth",
                                     NULL);

  gtk_editable_set_text (GTK_EDITABLE (self->dob_entry), self->dob ?: "");

  self->language = g_key_file_get_string (key_file,
                                          INFO_GROUP,
                                          "PreferredLanguage",
                                          NULL);

  gtk_editable_set_text (GTK_EDITABLE (self->pref_language_entry), self->language ?: "");

  self->home_address = g_key_file_get_string (key_file,
                                              INFO_GROUP,
                                              "HomeAddress",
                                              NULL);

  gtk_text_buffer_set_text (self->home_addr_text_buffer,
                            self->home_address ?: "",
                            -1);


  self->age = g_key_file_get_string (key_file,
                                     INFO_GROUP,
                                     "Age",
                                     NULL);

  gtk_editable_set_text (GTK_EDITABLE (self->age_entry), self->age ?: "");

  self->blood_type = g_key_file_get_string (key_file,
                                            INFO_GROUP,
                                            "BloodType",
                                            NULL);

  gtk_editable_set_text (GTK_EDITABLE (self->blood_type_entry), self->blood_type ?: "");

  self->height = g_key_file_get_string (key_file,
                                        INFO_GROUP,
                                        "Height",
                                        NULL);

  gtk_editable_set_text (GTK_EDITABLE (self->height_entry), self->height ?: "");

  self->weight = g_key_file_get_string (key_file,
                                        INFO_GROUP,
                                        "Weight",
                                        NULL);

  gtk_editable_set_text (GTK_EDITABLE (self->weight_entry), self->weight ?: "");

  temp_allergies = g_key_file_get_string_list (key_file,
                                               INFO_GROUP,
                                               "Allergies",
                                               NULL,
                                               NULL);

  if (temp_allergies)
    self->allergies = g_strjoinv ("\n", temp_allergies);

  gtk_text_buffer_set_text (self->allergies_text_buffer,
                            self->allergies ?: "",
                            -1);


  temp_med_cond = g_key_file_get_string_list (key_file,
                                              INFO_GROUP,
                                              "MedicationsAndConditions",
                                              NULL,
                                              NULL);

  if (temp_med_cond)
    self->medications_conditions = g_strjoinv ("\n", temp_med_cond);

  gtk_text_buffer_set_text (self->med_cond_text_buffer,
                            self->medications_conditions ?: "",
                            -1);

  self->other_info = g_key_file_get_string (key_file,
                                            INFO_GROUP,
                                            "OtherInfo",
                                            NULL);

  gtk_text_buffer_set_text (self->other_info_text_buffer,
                            self->other_info ?: "",
                            -1);

  self->contacts = g_key_file_get_keys (key_file,
                                        CONTACTS_GROUP,
                                        NULL,
                                        NULL);

  for (i = 0; self->contacts && self->contacts[i]; i++) {
    g_autofree char *number = NULL;
    number = g_key_file_get_string (key_file,
                                    CONTACTS_GROUP,
                                    self->contacts[i], NULL);
    if (number && *number) {
      add_contact_row (self, self->contacts[i], number);
    }
  }
}

static void
phosh_emergency_info_prefs_free_data (PhoshEmergencyInfoPrefs *self)
{
  g_clear_pointer (&self->owner_name, g_free);
  g_clear_pointer (&self->dob, g_free);
  g_clear_pointer (&self->language, g_free);
  g_clear_pointer (&self->home_address, g_free);
  g_clear_pointer (&self->age, g_free);
  g_clear_pointer (&self->blood_type, g_free);
  g_clear_pointer (&self->height, g_free);
  g_clear_pointer (&self->weight, g_free);
  g_clear_pointer (&self->allergies, g_free);
  g_clear_pointer (&self->other_info, g_free);
  g_clear_pointer (&self->medications_conditions, g_free);
}

static void
on_dialog_update_emer_contact (PhoshEmergencyInfoPrefs *self)
{
  g_autofree char *number_joined = NULL;
  const char *contact = NULL;
  const char *relationship = NULL;
  const char *number = NULL;
  g_autoptr (GKeyFile) key_file = g_key_file_new ();

  contact = gtk_editable_get_text (GTK_EDITABLE (self->contact_name_entry));
  relationship = gtk_editable_get_text (GTK_EDITABLE (self->relationship_entry));
  number = gtk_editable_get_text (GTK_EDITABLE (self->contact_number_entry));

  number_joined = g_strdup_printf ("%s;%s", number ?: "", relationship ?: "");

  add_contact_row (self, contact, number_joined);

  if (!g_key_file_load_from_file (key_file, self->keyfile_path, G_KEY_FILE_KEEP_COMMENTS, NULL))
    g_warning ("No Keyfile found at %s", self->keyfile_path);

  g_key_file_set_string (key_file,
                         CONTACTS_GROUP,
                         contact,
                         number_joined);

  save_keyfile (self, key_file);

  gtk_editable_set_text (GTK_EDITABLE (self->contact_name_entry), "");
  gtk_editable_set_text (GTK_EDITABLE (self->relationship_entry), "");
  gtk_editable_set_text (GTK_EDITABLE (self->contact_number_entry), "");

  adw_dialog_close (ADW_DIALOG (self->add_emer_contact_dialog));
}

static void
on_dialog_update_emer_contact_cancelled (PhoshEmergencyInfoPrefs *self)
{
  gtk_editable_set_text (GTK_EDITABLE (self->contact_name_entry), "");
  gtk_editable_set_text (GTK_EDITABLE (self->relationship_entry), "");
  gtk_editable_set_text (GTK_EDITABLE (self->contact_number_entry), "");

  adw_dialog_close (ADW_DIALOG (self->add_emer_contact_dialog));
}

static void
on_update_emer_contact (PhoshEmergencyInfoPrefs *self)
{
  adw_dialog_present (self->add_emer_contact_dialog, GTK_WIDGET (self));
}

static void
on_update_information_cancelled (PhoshEmergencyInfoPrefs *self)
{
  adw_dialog_close (ADW_DIALOG (self));
}

static void
on_update_information_clicked (PhoshEmergencyInfoPrefs *self)
{
  GtkTextIter start, end;

  phosh_emergency_info_prefs_free_data (self);

  self->owner_name = g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->owner_name_entry)));
  self->dob = g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->dob_entry)));
  self->language = g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->pref_language_entry)));

  gtk_text_buffer_get_start_iter (self->home_addr_text_buffer, &start);
  gtk_text_buffer_get_end_iter (self->home_addr_text_buffer, &end);
  self->home_address = gtk_text_buffer_get_text (self->home_addr_text_buffer,
                                                 &start, &end, true);

  self->age = g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->age_entry)));
  self->blood_type = g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->blood_type_entry)));
  self->height = g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->height_entry)));
  self->weight = g_strdup (gtk_editable_get_text (GTK_EDITABLE (self->weight_entry)));

  gtk_text_buffer_get_start_iter (self->allergies_text_buffer, &start);
  gtk_text_buffer_get_end_iter (self->allergies_text_buffer, &end);
  self->allergies = gtk_text_buffer_get_text (self->allergies_text_buffer, &start, &end, true);

  gtk_text_buffer_get_start_iter (self->med_cond_text_buffer, &start);
  gtk_text_buffer_get_end_iter (self->med_cond_text_buffer, &end);
  self->medications_conditions = gtk_text_buffer_get_text (self->med_cond_text_buffer,
                                                           &start, &end, true);

  gtk_text_buffer_get_start_iter (self->other_info_text_buffer, &start);
  gtk_text_buffer_get_end_iter (self->other_info_text_buffer, &end);
  self->other_info = gtk_text_buffer_get_text (self->other_info_text_buffer,
                                               &start, &end, true);

  save_settings (self);

  adw_dialog_close (ADW_DIALOG (self));
}


static void
phosh_emergency_info_prefs_finalize (GObject *object)
{
  PhoshEmergencyInfoPrefs *self = PHOSH_EMERGENCY_INFO_PREFS (object);

  phosh_emergency_info_prefs_free_data (self);
  g_clear_pointer (&self->contacts, g_strfreev);
  g_clear_pointer (&self->keyfile_path, g_free);

  G_OBJECT_CLASS (phosh_emergency_info_prefs_parent_class)->finalize (object);
}


static void
phosh_emergency_info_prefs_class_init (PhoshEmergencyInfoPrefsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->finalize = phosh_emergency_info_prefs_finalize;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/mobi/phosh/plugins/emergency-info-prefs/emergency-info-prefs.ui");

  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, home_addr_text_buffer);
  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, allergies_text_buffer);
  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, med_cond_text_buffer);
  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, other_info_text_buffer);

  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, owner_name_entry);
  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, dob_entry);
  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, pref_language_entry);

  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, age_entry);
  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, blood_type_entry);
  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, height_entry);
  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, weight_entry);

  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, contact_name_entry);
  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, relationship_entry);
  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, contact_number_entry);

  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, add_emer_contact_dialog);
  gtk_widget_class_bind_template_child (widget_class, PhoshEmergencyInfoPrefs, emer_contacts);

  gtk_widget_class_bind_template_callback (widget_class, on_update_emer_contact);
  gtk_widget_class_bind_template_callback (widget_class, on_update_information_clicked);
  gtk_widget_class_bind_template_callback (widget_class, on_update_information_cancelled);
  gtk_widget_class_bind_template_callback (widget_class, on_dialog_update_emer_contact);
  gtk_widget_class_bind_template_callback (widget_class, on_dialog_update_emer_contact_cancelled);
}


static void
phosh_emergency_info_prefs_init (PhoshEmergencyInfoPrefs *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->keyfile_path = g_build_filename (g_get_user_config_dir (),
                                         EMERGENCY_INFO_GKEYFILE_LOCATION,
                                         EMERGENCY_INFO_GKEYFILE_NAME,
                                         NULL);
  load_settings (self);
}
