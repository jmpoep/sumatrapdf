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

package com.artifex.mupdf.fitz;

public class DocumentWriter
{
	static {
		Context.init();
	}

	private long pointer;

	protected native void finalize();

	public void destroy() {
		finalize();
	}

	private native static long newNativeDocumentWriter(String filename, String format, String options);
	private native static long newNativeDocumentWriterWithSeekableOutputStream(SeekableOutputStream stream, String format, String options);
	private native static long newNativeDocumentWriterWithBuffer(Buffer buffer, String format, String options);

	public DocumentWriter(String filename, String format, String options) {
		pointer = newNativeDocumentWriter(filename, format, options);
	}

	public DocumentWriter(SeekableOutputStream stream, String format, String options) {
		pointer = newNativeDocumentWriterWithSeekableOutputStream(stream, format, options);
	}

	public DocumentWriter(Buffer buffer, String format, String options) {
		pointer = newNativeDocumentWriterWithBuffer(buffer, format, options);
	}

	public native Device beginPage(Rect mediabox);
	public native void endPage();
	public native void close();

	private long ocrlistener;

	public interface OCRListener
	{
		boolean progress(int page, int percent);
	}

	public native void addOCRListener(OCRListener listener);
}
