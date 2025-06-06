// Copyright (C) 2004-2025 Artifex Software, Inc.
//
// This file is part of MuPDF.
//
// MuPDF is free software: you can redistribute it and/or modify it under the
// terms of the GNU Affero General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// MuPDF is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
// details.
//
// You should have received a copy of the GNU Affero General Public License
// along with MuPDF. If not, see <https://www.gnu.org/licenses/agpl-3.0.en.html>
//
// Alternative licensing terms are available from the licensor.
// For commercial licensing, see <https://www.artifex.com/> or contact
// Artifex Software, Inc., 39 Mesa Street, Suite 108A, San Francisco,
// CA 94129, USA, for further information.

#include "mupdf/fitz.h"
#include "mupdf/pdf.h"

#include <string.h>

static pdf_obj *
pdf_lookup_name_imp(fz_context *ctx, pdf_obj *node, const char *needle, pdf_cycle_list *cycle_up)
{
	pdf_cycle_list cycle;
	pdf_obj *kids = pdf_dict_get(ctx, node, PDF_NAME(Kids));
	pdf_obj *names = pdf_dict_get(ctx, node, PDF_NAME(Names));

	if (pdf_cycle(ctx, &cycle, cycle_up, node))
		return NULL;

	if (pdf_is_array(ctx, kids))
	{
		int l = 0;
		int r = pdf_array_len(ctx, kids) - 1;

		while (l <= r)
		{
			int m = (l + r) >> 1;
			pdf_obj *kid = pdf_array_get(ctx, kids, m);
			pdf_obj *limits = pdf_dict_get(ctx, kid, PDF_NAME(Limits));
			const char *first = pdf_array_get_text_string(ctx, limits, 0);
			const char *last = pdf_array_get_text_string(ctx, limits, 1);

			if (!pdf_is_indirect(ctx, kid))
			{
				fz_warn(ctx, "non-indirect internal node found in name tree");
				break;
			}

			if (strcmp(needle, first) < 0)
				r = m - 1;
			else if (strcmp(needle, last) > 0)
				l = m + 1;
			else
			{
				pdf_obj *obj = pdf_lookup_name_imp(ctx, kid, needle, &cycle);
				if (obj)
					return obj;
				else
					break;
			}
		}

		/* Spec says names should be sorted (hence the binary search,
		 * above), but Acrobat copes with non-sorted. Drop back to a
		 * simple search if the binary search fails. */
		r = pdf_array_len(ctx, kids);
		for (l = 0; l < r; l++)
		{
			pdf_obj *obj, *kid = pdf_array_get(ctx, kids, l);
			if (!pdf_is_indirect(ctx, kid))
			{
				fz_warn(ctx, "non-indirect internal node found in name tree");
				continue;
			}
			obj = pdf_lookup_name_imp(ctx, kid, needle, &cycle);
			if (obj)
				return obj;
		}
	}

	if (pdf_is_array(ctx, names))
	{
		int l = 0;
		int r = (pdf_array_len(ctx, names) / 2) - 1;

		while (l <= r)
		{
			int m = (l + r) >> 1;
			int c;
			const char *key = pdf_array_get_text_string(ctx, names, m * 2);
			pdf_obj *val = pdf_array_get(ctx, names, m * 2 + 1);

			c = strcmp(needle, key);
			if (c < 0)
				r = m - 1;
			else if (c > 0)
				l = m + 1;
			else
				return val;
		}

		/* Spec says names should be sorted (hence the binary search,
		 * above), but Acrobat copes with non-sorted. Drop back to a
		 * simple search if the binary search fails. */
		r = pdf_array_len(ctx, names)/2;
		for (l = 0; l < r; l++)
			if (!strcmp(needle, pdf_array_get_text_string(ctx, names, l * 2)))
				return pdf_array_get(ctx, names, l * 2 + 1);
	}

	return NULL;
}

pdf_obj *
pdf_lookup_name(fz_context *ctx, pdf_document *doc, pdf_obj *which, pdf_obj *needle)
{
	pdf_obj *root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
	pdf_obj *names = pdf_dict_get(ctx, root, PDF_NAME(Names));
	pdf_obj *tree = pdf_dict_get(ctx, names, which);
	return pdf_lookup_name_imp(ctx, tree, pdf_to_text_string(ctx, needle), NULL);
}

pdf_obj *
pdf_lookup_dest(fz_context *ctx, pdf_document *doc, pdf_obj *needle)
{
	pdf_obj *root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
	pdf_obj *dests = pdf_dict_get(ctx, root, PDF_NAME(Dests));
	pdf_obj *names = pdf_dict_get(ctx, root, PDF_NAME(Names));

	/* PDF 1.1 has destinations in a dictionary */
	if (dests)
	{
		if (pdf_is_name(ctx, needle))
			return pdf_dict_get(ctx, dests, needle);
		else
			return pdf_dict_gets(ctx, dests, pdf_to_str_buf(ctx, needle));
	}

	/* PDF 1.2 has destinations in a name tree */
	if (names)
	{
		pdf_obj *tree = pdf_dict_get(ctx, names, PDF_NAME(Dests));
		return pdf_lookup_name_imp(ctx, tree, pdf_to_text_string(ctx, needle), NULL);
	}

	return NULL;
}

static void
pdf_load_name_tree_imp(fz_context *ctx, pdf_obj *dict, pdf_document *doc, pdf_obj *node, pdf_cycle_list *cycle_up)
{
	pdf_cycle_list cycle;
	pdf_obj *kids = pdf_dict_get(ctx, node, PDF_NAME(Kids));
	pdf_obj *names = pdf_dict_get(ctx, node, PDF_NAME(Names));
	int i;

	if (kids && !pdf_cycle(ctx, &cycle, cycle_up, node))
	{
		int len = pdf_array_len(ctx, kids);
		for (i = 0; i < len; i++)
			pdf_load_name_tree_imp(ctx, dict, doc, pdf_array_get(ctx, kids, i), &cycle);
	}

	if (names)
	{
		int len = pdf_array_len(ctx, names);
		for (i = 0; i + 1 < len; i += 2)
		{
			pdf_obj *key = pdf_array_get(ctx, names, i);
			pdf_obj *val = pdf_array_get(ctx, names, i + 1);
			if (pdf_is_string(ctx, key))
			{
				key = pdf_new_name(ctx, pdf_to_text_string(ctx, key));
				fz_try(ctx)
					pdf_dict_put(ctx, dict, key, val);
				fz_always(ctx)
					pdf_drop_obj(ctx, key);
				fz_catch(ctx)
					fz_rethrow(ctx);
			}
			else if (pdf_is_name(ctx, key))
			{
				pdf_dict_put(ctx, dict, key, val);
			}
		}
	}
}

/* FIXME: fz_try/fz_catch needed here */
pdf_obj *
pdf_load_name_tree(fz_context *ctx, pdf_document *doc, pdf_obj *which)
{
	pdf_obj *root = pdf_dict_get(ctx, pdf_trailer(ctx, doc), PDF_NAME(Root));
	pdf_obj *names = pdf_dict_get(ctx, root, PDF_NAME(Names));
	pdf_obj *tree = pdf_dict_get(ctx, names, which);
	if (pdf_is_dict(ctx, tree))
	{
		pdf_obj *dict = pdf_new_dict(ctx, doc, 100);
		pdf_load_name_tree_imp(ctx, dict, doc, tree, NULL);
		return dict;
	}
	return NULL;
}

pdf_obj *
pdf_lookup_number_imp(fz_context *ctx, pdf_obj *node, int needle, pdf_cycle_list *cycle_up)
{
	pdf_cycle_list cycle;
	pdf_obj *kids = pdf_dict_get(ctx, node, PDF_NAME(Kids));
	pdf_obj *nums = pdf_dict_get(ctx, node, PDF_NAME(Nums));

	if (pdf_is_array(ctx, kids))
	{
		int l = 0;
		int r = pdf_array_len(ctx, kids) - 1;

		while (l <= r)
		{
			int m = (l + r) >> 1;
			pdf_obj *kid = pdf_array_get(ctx, kids, m);
			pdf_obj *limits = pdf_dict_get(ctx, kid, PDF_NAME(Limits));
			int first = pdf_array_get_int(ctx, limits, 0);
			int last = pdf_array_get_int(ctx, limits, 1);

			if (needle < first)
				r = m - 1;
			else if (needle > last)
				l = m + 1;
			else
			{
				if (pdf_cycle(ctx, &cycle, cycle_up, node))
					break;
				return pdf_lookup_number_imp(ctx, kid, needle, &cycle);
			}
		}
	}

	if (pdf_is_array(ctx, nums))
	{
		int l = 0;
		int r = (pdf_array_len(ctx, nums) / 2) - 1;

		while (l <= r)
		{
			int m = (l + r) >> 1;
			int key = pdf_array_get_int(ctx, nums, m * 2);
			pdf_obj *val = pdf_array_get(ctx, nums, m * 2 + 1);

			if (needle < key)
				r = m - 1;
			else if (needle > key)
				l = m + 1;
			else
				return val;
		}

		/* Parallel the nametree lookup above by allowing for non-sorted lists. */
		r = pdf_array_len(ctx, nums)/2;
		for (l = 0; l < r; l++)
			if (needle == pdf_array_get_int(ctx, nums, l * 2))
				return pdf_array_get(ctx, nums, l * 2 + 1);
	}

	return NULL;
}

pdf_obj *
pdf_lookup_number(fz_context *ctx, pdf_obj *node, int needle)
{
	return pdf_lookup_number_imp(ctx, node, needle, NULL);
}

static void pdf_walk_tree_imp(fz_context *ctx, pdf_obj *obj, pdf_obj *kid_name,
			void (*arrive)(fz_context *, pdf_obj *, void *, pdf_obj **),
			void (*leave)(fz_context *, pdf_obj *, void *),
			void *arg,
			pdf_obj **inherit_names,
			pdf_obj **inherit_vals,
			pdf_cycle_list *cycle_up);

static void
pdf_walk_tree_kid(fz_context *ctx,
			pdf_obj *obj,
			pdf_obj *kid_name,
			void (*arrive)(fz_context *, pdf_obj *, void *, pdf_obj **),
			void (*leave)(fz_context *, pdf_obj *, void *),
			void *arg,
			pdf_obj **inherit_names,
			pdf_obj **inherit_vals,
			pdf_cycle_list *cycle_up)
{
	pdf_cycle_list cycle;
	pdf_obj **new_vals = NULL;

	if (obj == NULL || pdf_cycle(ctx, &cycle, cycle_up, obj))
		return;

	fz_var(new_vals);

	fz_try(ctx)
	{
		/* First we run through the names we've been asked to collect
		 * inherited values for updating the values. */
		if (inherit_names != NULL)
		{
			int i, n;

			for (n = 0; inherit_names[n] != NULL; n++);

			for (i = 0; i < n; i++)
			{
				pdf_obj *v = pdf_dict_get(ctx, obj, inherit_names[i]);
				if (v != NULL)
				{
					if (new_vals == NULL)
					{
						new_vals = fz_malloc_array(ctx, n, pdf_obj *);
						memcpy(new_vals, inherit_vals, n*sizeof(pdf_obj *));
						inherit_vals = new_vals;
					}
					inherit_vals[i] = v;
				}
			}
		}

		if (arrive)
			arrive(ctx, obj, arg, inherit_vals);
		pdf_walk_tree_imp(ctx, pdf_dict_get(ctx, obj, kid_name), kid_name, arrive, leave, arg, inherit_names, inherit_vals, &cycle);
		if (leave)
			leave(ctx, obj, arg);
	}
	fz_always(ctx)
		fz_free(ctx, new_vals);
	fz_catch(ctx)
		fz_rethrow(ctx);
}

static void pdf_walk_tree_imp(fz_context *ctx, pdf_obj *obj, pdf_obj *kid_name,
			void (*arrive)(fz_context *, pdf_obj *, void *, pdf_obj **),
			void (*leave)(fz_context *, pdf_obj *, void *),
			void *arg,
			pdf_obj **inherit_names,
			pdf_obj **inherit_vals,
			pdf_cycle_list *cycle_up)
{
	pdf_cycle_list cycle;

	if (obj == NULL || pdf_cycle(ctx, &cycle, cycle_up, obj))
		return;

	if (pdf_is_array(ctx, obj))
	{
		int i, n = pdf_array_len(ctx, obj);
		for (i = 0; i < n; i++)
			pdf_walk_tree_kid(ctx, pdf_array_get(ctx, obj, i), kid_name, arrive, leave, arg, inherit_names, inherit_vals, &cycle);
	}
	else
	{
		pdf_walk_tree_kid(ctx, obj, kid_name, arrive, leave, arg, inherit_names, inherit_vals, &cycle);
	}
}

void pdf_walk_tree(fz_context *ctx, pdf_obj *obj, pdf_obj *kid_name,
			void (*arrive)(fz_context *, pdf_obj *, void *, pdf_obj **),
			void (*leave)(fz_context *, pdf_obj *, void *),
			void *arg,
			pdf_obj **inherit_names,
			pdf_obj **inherit_vals)
{
	pdf_walk_tree_imp(ctx, obj, kid_name, arrive, leave, arg, inherit_names, inherit_vals, NULL);
}
