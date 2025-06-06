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

/* This code needs to be kept out of stm_buffer.c to avoid it being
 * pulled into cmapdump.c */

fz_compressed_buffer *
fz_keep_compressed_buffer(fz_context *ctx, fz_compressed_buffer *cbuf)
{
	return (fz_compressed_buffer *)fz_keep_imp(ctx, cbuf, &cbuf->refs);
}

void
fz_drop_compressed_buffer(fz_context *ctx, fz_compressed_buffer *buf)
{
	if (fz_drop_imp(ctx, buf, &buf->refs))
	{
		if (buf->params.type == FZ_IMAGE_JBIG2)
			fz_drop_jbig2_globals(ctx, buf->params.u.jbig2.globals);
		fz_drop_buffer(ctx, buf->buffer);
		fz_free(ctx, buf);
	}
}

fz_compressed_buffer *
fz_new_compressed_buffer(fz_context *ctx)
{
	fz_compressed_buffer *cbuf = fz_malloc_struct(ctx, fz_compressed_buffer);

	cbuf->refs = 1;

	return cbuf;
}

fz_stream *
fz_open_image_decomp_stream_from_buffer(fz_context *ctx, fz_compressed_buffer *buffer, int *l2factor)
{
	fz_stream *head, *tail;

	tail = fz_open_buffer(ctx, buffer->buffer);
	fz_try(ctx)
		head = fz_open_image_decomp_stream(ctx, tail, &buffer->params, l2factor);
	fz_always(ctx)
		fz_drop_stream(ctx, tail);
	fz_catch(ctx)
		fz_rethrow(ctx);
	return head;
}

fz_stream *
fz_open_image_decomp_stream(fz_context *ctx, fz_stream *tail, fz_compression_params *params, int *l2factor)
{
	fz_stream *head = NULL, *body = NULL;
	int our_l2factor = 0;

	fz_var(body);

	fz_try(ctx)
	{
		switch (params->type)
		{
		default:
			head = fz_keep_stream(ctx, tail);
			break;

		case FZ_IMAGE_FAX:
			head = fz_open_faxd(ctx, tail,
					params->u.fax.k,
					params->u.fax.end_of_line,
					params->u.fax.encoded_byte_align,
					params->u.fax.columns,
					params->u.fax.rows,
					params->u.fax.end_of_block,
					params->u.fax.black_is_1);
			break;

		case FZ_IMAGE_JPEG:
			if (l2factor)
			{
				our_l2factor = *l2factor;
				if (our_l2factor > 3)
					our_l2factor = 3;
				*l2factor -= our_l2factor;
			}
			head = fz_open_dctd(ctx, tail, params->u.jpeg.color_transform, params->u.jpeg.invert_cmyk, our_l2factor, NULL);
			break;

		case FZ_IMAGE_JBIG2:
			head = fz_open_jbig2d(ctx, tail, params->u.jbig2.globals, params->u.jbig2.embedded);
			break;

		case FZ_IMAGE_RLD:
			head = fz_open_rld(ctx, tail);
			break;

		case FZ_IMAGE_FLATE:
			head = fz_open_flated(ctx, tail, 15);
			if (params->u.flate.predictor > 1)
			{
				body = head;
				head = fz_open_predict(ctx, body,
						params->u.flate.predictor,
						params->u.flate.columns,
						params->u.flate.colors,
						params->u.flate.bpc);
			}
			break;

		case FZ_IMAGE_BROTLI:
			head = fz_open_brotlid(ctx, tail);
			if (params->u.brotli.predictor > 1)
			{
				body = head;
				head = fz_open_predict(ctx, body,
						params->u.brotli.predictor,
						params->u.brotli.columns,
						params->u.brotli.colors,
						params->u.brotli.bpc);
			}
			break;

		case FZ_IMAGE_LZW:
			head = fz_open_lzwd(ctx, tail, params->u.lzw.early_change, 9, 0, 0);
			if (params->u.flate.predictor > 1)
			{
				body = head;
				head = fz_open_predict(ctx, body,
						params->u.lzw.predictor,
						params->u.lzw.columns,
						params->u.lzw.colors,
						params->u.lzw.bpc);
			}
			break;
		}
	}
	fz_always(ctx)
		fz_drop_stream(ctx, body);
	fz_catch(ctx)
		fz_rethrow(ctx);

	return head;
}

fz_stream *
fz_open_compressed_buffer(fz_context *ctx, fz_compressed_buffer *buffer)
{
	return fz_open_image_decomp_stream_from_buffer(ctx, buffer, NULL);
}

size_t
fz_compressed_buffer_size(fz_compressed_buffer *buffer)
{
	if (!buffer)
		return 0;
	if (buffer->buffer)
		return (size_t)buffer->buffer->cap + sizeof(*buffer);
	return sizeof(*buffer);
}
