/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors:
 *	parthasrathi susarla <sparthasrathi@novell.com>
 * Based on the IMAP summary class implementation by:
 *    Michael Zucchi <notzed@ximian.com>
 *    Dan Winship <danw@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "camel-ews-folder.h"
#include "camel-ews-summary.h"

#define CAMEL_EWS_SUMMARY_VERSION (1)

#define EXTRACT_FIRST_DIGIT(val) part ? val=strtoul (part, &part, 10) : 0;
#define EXTRACT_DIGIT(val) part++; part ? val=strtoul (part, &part, 10) : 0;

#define d(x)

/*Prototypes*/
static gboolean ews_info_set_flags(CamelMessageInfo *info, guint32 flags, guint32 set);

static gboolean summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir);
static CamelFIRecord * summary_header_to_db (CamelFolderSummary *s, GError **error);
static CamelMIRecord * message_info_to_db (CamelFolderSummary *s, CamelMessageInfo *info);
static CamelMessageInfo * message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);
static gboolean content_info_to_db (CamelFolderSummary *s, CamelMessageContentInfo *info, CamelMIRecord *mir);
static CamelMessageContentInfo * content_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir);

/*End of Prototypes*/

G_DEFINE_TYPE (CamelEwsSummary, camel_ews_summary, CAMEL_TYPE_FOLDER_SUMMARY)

static CamelMessageInfo *
ews_message_info_clone(CamelFolderSummary *s, const CamelMessageInfo *mi)
{
	CamelEwsMessageInfo *to;
	const CamelEwsMessageInfo *from = (const CamelEwsMessageInfo *)mi;

	to = (CamelEwsMessageInfo *)CAMEL_FOLDER_SUMMARY_CLASS (camel_ews_summary_parent_class)->message_info_clone(s, mi);
	to->server_flags = from->server_flags;
	to->item_type = from->item_type;
	to->change_key = g_strdup (from->change_key);

	/* FIXME: parent clone should do this */
	to->info.content = camel_folder_summary_content_info_new(s);

	return (CamelMessageInfo *)to;
}

static void
ews_message_info_free (CamelFolderSummary *s, CamelMessageInfo *mi)
{
	CamelEwsMessageInfo *emi = (void *)mi;

	g_free (emi->change_key);
	CAMEL_FOLDER_SUMMARY_CLASS (camel_ews_summary_parent_class)->message_info_free (s, mi);
}

static void
ews_summary_finalize (GObject *object)
{
       CamelEwsSummary *ews_summary = CAMEL_EWS_SUMMARY (object);

       g_free (ews_summary->sync_state);

       /* Chain up to parent's finalize() method. */
       G_OBJECT_CLASS (camel_ews_summary_parent_class)->finalize (object);
}

static void
camel_ews_summary_class_init (CamelEwsSummaryClass *class)
{
	CamelFolderSummaryClass *folder_summary_class;
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = ews_summary_finalize;

	folder_summary_class = CAMEL_FOLDER_SUMMARY_CLASS (class);
	folder_summary_class->message_info_size = sizeof (CamelEwsMessageInfo);
	folder_summary_class->content_info_size = sizeof (CamelEwsMessageContentInfo);
	folder_summary_class->message_info_clone = ews_message_info_clone;
	folder_summary_class->message_info_free = ews_message_info_free;
	folder_summary_class->info_set_flags = ews_info_set_flags;
	folder_summary_class->summary_header_to_db = summary_header_to_db;
	folder_summary_class->summary_header_from_db = summary_header_from_db;
	folder_summary_class->message_info_to_db = message_info_to_db;
	folder_summary_class->message_info_from_db = message_info_from_db;
	folder_summary_class->content_info_to_db = content_info_to_db;
	folder_summary_class->content_info_from_db = content_info_from_db;
}

static void
camel_ews_summary_init (CamelEwsSummary *ews_summary)
{
}

/**
 * camel_ews_summary_new:
 *
 * This will create a new CamelEwsSummary object and read in the
 * summary data from disk, if it exists.
 *
 * Returns: A new CamelEwsSummary object.
 **/
CamelFolderSummary *
camel_ews_summary_new (struct _CamelFolder *folder)
{
	CamelFolderSummary *summary;

	summary = g_object_new (CAMEL_TYPE_EWS_SUMMARY, "folder", folder, NULL);
	camel_folder_summary_set_build_content (summary, TRUE);

	camel_folder_summary_load_from_db (summary, NULL);

	return summary;
}

static gboolean
summary_header_from_db (CamelFolderSummary *s, CamelFIRecord *mir)
{
	CamelEwsSummary *gms = CAMEL_EWS_SUMMARY (s);
	gchar *part;

	if (!CAMEL_FOLDER_SUMMARY_CLASS (camel_ews_summary_parent_class)->summary_header_from_db (s, mir))
		return FALSE;

	part = mir->bdata;

	if (part)
		EXTRACT_FIRST_DIGIT(gms->version);

	if (part && part++ && strcmp (part, "(null)")) {
		gms->sync_state = g_strdup (part);
	}

	return TRUE;
}

static CamelFIRecord *
summary_header_to_db (CamelFolderSummary *s, GError **error)
{
	CamelEwsSummary *ims = CAMEL_EWS_SUMMARY(s);
	struct _CamelFIRecord *fir;

	fir = CAMEL_FOLDER_SUMMARY_CLASS (camel_ews_summary_parent_class)->summary_header_to_db (s, error);
	if (!fir)
		return NULL;

	fir->bdata = g_strdup_printf ("%d %s", CAMEL_EWS_SUMMARY_VERSION, ims->sync_state);

	return fir;

}

static CamelMessageInfo *
message_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir)
{
	CamelMessageInfo *info;
	CamelEwsMessageInfo *iinfo;

	info = CAMEL_FOLDER_SUMMARY_CLASS (camel_ews_summary_parent_class)->message_info_from_db (s, mir);
	if (info) {
		gchar *part = mir->bdata;
		gchar **values;

		iinfo = (CamelEwsMessageInfo *)info;
		values = g_strsplit (part, " ", -1);

		iinfo->server_flags = g_ascii_strtoll (values [0], NULL, 10);
		iinfo->item_type = g_ascii_strtoll (values [1], NULL, 10);
		iinfo->change_key = g_strdup (values [2]);

		g_strfreev (values);
	}

	return info;
}

static CamelMIRecord *
message_info_to_db (CamelFolderSummary *s, CamelMessageInfo *info)
{
	CamelEwsMessageInfo *iinfo = (CamelEwsMessageInfo *)info;
	struct _CamelMIRecord *mir;

	mir = CAMEL_FOLDER_SUMMARY_CLASS (camel_ews_summary_parent_class)->message_info_to_db (s, info);
	if (mir)
		mir->bdata = g_strdup_printf ("%u %d %s", iinfo->server_flags, iinfo->item_type, iinfo->change_key);

	return mir;
}

static CamelMessageContentInfo *
content_info_from_db (CamelFolderSummary *s, CamelMIRecord *mir)
{
	gchar *part = mir->cinfo;
	guint32 type=0;

	if (part) {
		if (*part == ' ')
			part++;
		if (part) {
			EXTRACT_FIRST_DIGIT (type);
		}
	}
	mir->cinfo = part;
	if (type)
		return CAMEL_FOLDER_SUMMARY_CLASS (camel_ews_summary_parent_class)->content_info_from_db (s, mir);
	else
		return camel_folder_summary_content_info_new (s);
}

static gboolean
content_info_to_db (CamelFolderSummary *s, CamelMessageContentInfo *info, CamelMIRecord *mir)
{

	if (info->type) {
		mir->cinfo = g_strdup ("1");
		return CAMEL_FOLDER_SUMMARY_CLASS (camel_ews_summary_parent_class)->content_info_to_db (s, info, mir);
	} else {
		mir->cinfo = g_strdup ("0");
		return TRUE;
	}
}

static gboolean
ews_info_set_flags (CamelMessageInfo *info, guint32 flags, guint32 set)
{
	return CAMEL_FOLDER_SUMMARY_CLASS (camel_ews_summary_parent_class)->info_set_flags (info, flags, set);
}

void
camel_ews_summary_add_message	(CamelFolderSummary *summary,
				 const gchar *uid,
				 CamelMimeMessage *message)
{
	CamelEwsMessageInfo *mi;
	CamelMessageInfo *info;
	const CamelFlag *flag;
	const CamelTag *tag;

	info = camel_folder_summary_get (summary, uid);

	/* Create summary entry */
	mi = (CamelEwsMessageInfo *)camel_folder_summary_info_new_from_message (summary, message, NULL);

	/* Copy flags 'n' tags */
	mi->info.flags = camel_message_info_flags(info);

	flag = camel_message_info_user_flags(info);
	while (flag) {
		camel_message_info_set_user_flag((CamelMessageInfo *)mi, flag->name, TRUE);
		flag = flag->next;
	}
	tag = camel_message_info_user_tags(info);
	while (tag) {
		camel_message_info_set_user_tag((CamelMessageInfo *)mi, tag->name, tag->value);
		tag = tag->next;
	}

	mi->info.size = camel_message_info_size(info);
	mi->info.uid = camel_pstring_strdup (uid);

	camel_folder_summary_add (summary, (CamelMessageInfo *)mi);
	camel_message_info_free (info);
}

void
camel_ews_summary_add_message_info	(CamelFolderSummary *summary,
					 guint32 server_flags,
					 CamelMessageInfo *mi)
{
	CamelMessageInfoBase *binfo = (CamelMessageInfoBase *) mi;
	CamelEwsMessageInfo *einfo = (CamelEwsMessageInfo *) mi;

	binfo->flags |= server_flags;
	einfo->server_flags = server_flags;

	/* TODO update user flags */

	binfo->flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
	camel_folder_summary_add (summary, (CamelMessageInfo *)mi);
}

static gboolean
ews_update_user_flags (CamelMessageInfo *info, CamelFlag *server_user_flags)
{
	gboolean changed = FALSE;
	CamelMessageInfoBase *binfo = (CamelMessageInfoBase *) info;
	gboolean set_cal = FALSE;

	if (camel_flag_get (&binfo->user_flags, "$has_cal"))
		set_cal = TRUE;

	changed = camel_flag_list_copy (&binfo->user_flags, &server_user_flags);

	/* reset the calendar flag if it was set in messageinfo before */
	if (set_cal)
		camel_flag_set (&binfo->user_flags, "$has_cal", TRUE);

	return changed;
}

gboolean
camel_ews_update_message_info_flags	(CamelFolderSummary *summary,
					 CamelMessageInfo *info,
					 guint32 server_flags,
					 CamelFlag *server_user_flags)
{
	CamelEwsMessageInfo *einfo = (CamelEwsMessageInfo *) info;
	gboolean changed = FALSE;

	if (server_flags != einfo->server_flags) {
		guint32 server_set, server_cleared;

		server_set = server_flags & ~einfo->server_flags;
		server_cleared = einfo->server_flags & ~server_flags;

		camel_message_info_set_flags (info, server_set | server_cleared, (einfo->info.flags | server_set) & ~server_cleared);
		einfo->server_flags = server_flags;
		if (info->summary)
			camel_folder_summary_touch (info->summary);
		changed = TRUE;
	}

	/* TODO test user_flags after enabling it */
	if (server_user_flags && ews_update_user_flags (info, server_user_flags))
		changed = TRUE;

	return changed;
}

void
ews_summary_clear	(CamelFolderSummary *summary,
			 gboolean uncache)
{
	CamelFolderChangeInfo *changes;
	GPtrArray *known_uids;
	gint i;

	changes = camel_folder_change_info_new ();
	known_uids = camel_folder_summary_get_array (summary);
	for (i = 0; i < known_uids->len; i++) {
		const gchar *uid = g_ptr_array_index (known_uids, i);

		if (!uid)
			continue;

		camel_folder_change_info_remove_uid (changes, uid);
		camel_folder_summary_remove_uid (summary, uid);
	}

	camel_folder_summary_clear (summary, NULL);
	/*camel_folder_summary_save (summary);*/

	if (camel_folder_change_info_changed (changes))
		camel_folder_changed (camel_folder_summary_get_folder (summary), changes);
	camel_folder_change_info_free (changes);
	camel_folder_summary_free_array (known_uids);
}

