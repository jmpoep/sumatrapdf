# Buffers & Streams

## Buffers

In order to represent a generic chunks of data we use the `fz_buffer` structure.

	typedef struct {
		unsigned char *data;
		size_t len; // current length
		size_t cap; // total capacity
		... reserved internal fields ...
	} fz_buffer;

	fz_buffer *fz_keep_buffer(fz_context *ctx, fz_buffer *buf);
	void fz_drop_buffer(fz_context *ctx, fz_buffer *buf);

There are many ways to create a buffer. Some create a buffer shared immutable data, others with data you can edit, and others by copying or decoding other data.

`fz_buffer *fz_new_buffer(fz_context *ctx, size_t capacity);`
:	Create a new empty buffer, with the given initial capacity.

`fz_buffer *fz_new_buffer_from_shared_data(fz_context *ctx, const unsigned char *data, size_t size);`
:	Create a new buffer wrapping the given data pointer. The data is only referenced, and will not be freed when the buffer is destroyed. The data pointer must **not** change or disappear while the buffer lives.

`fz_buffer *fz_new_buffer_from_copied_data(fz_context *ctx, const unsigned char *data, size_t size);`
:	Create a buffer containing a copy of the data pointed to.

`fz_buffer *fz_new_buffer_from_base64(fz_context *ctx, const char *data, size_t size);`
:	Create a buffer containing the decoded `BASE64` data.

Sometimes you want to create buffers piece by piece by appending strings or other data to it, while dynamically growing the underlying storage.

	void fz_append_data(fz_context *ctx, fz_buffer *buf, const void *data, size_t len);
	void fz_append_string(fz_context *ctx, fz_buffer *buf, const char *string);
	void fz_append_byte(fz_context *ctx, fz_buffer *buf, int byte);
	void fz_append_rune(fz_context *ctx, fz_buffer *buf, int rune);
	void fz_append_int16_be(fz_context *ctx, fz_buffer *buf, int x);
	void fz_append_int16_le(fz_context *ctx, fz_buffer *buf, int x);
	void fz_append_int32_be(fz_context *ctx, fz_buffer *buf, int x);
	void fz_append_int32_le(fz_context *ctx, fz_buffer *buf, int x);
	void fz_append_printf(fz_context *ctx, fz_buffer *buffer, const char *fmt, ...);
	void fz_append_vprintf(fz_context *ctx, fz_buffer *buffer, const char *fmt, va_list args);

You can also write a bit stream with the following functions. The buffer length always covers all the bits in the buffer, including any unused ones in the last byte, which will always be zero.

`void fz_append_bits(fz_context *ctx, fz_buffer *buf, int value, int count);`
:	Write the lower count bits from value into the buffer.

`void fz_append_bits_pad(fz_context *ctx, fz_buffer *buf);`
:	Write enough zero bits to be byte aligned.

You can use the buffer data as a zero-terminated C string by calling the following function. This will ensure that there is a zero terminator after the last byte and return a pointer to the first byte. This pointer is only borrowed, and should only be used briefly, before the buffer is changed again (which may reallocate or free the data).

`const char *fz_string_from_buffer(fz_context *ctx, fz_buffer *buf);`
:	You can also read and write the contents of a buffer to file.

`fz_buffer *fz_read_file(fz_context *ctx, const char *filename);`
:	Read the contents of a file into a buffer.

`void fz_save_buffer(fz_context *ctx, fz_buffer *buf, const char *filename);`
:	Save the contents of a buffer to a file.

## Input Streams

An input stream reads data from a source. Some stream types can decompress and decrypt data, and these streams can be chained together in a pipeline.

	typedef struct { internal } fz_stream;

	fz_stream *fz_keep_stream(fz_context *ctx, fz_stream *stm);
	void fz_drop_stream(fz_context *ctx, fz_stream *stm);

`fz_stream *fz_open_file(fz_context *ctx, const char *filename);`
:	Open a stream reading the contents of a file.

`fz_stream *fz_open_memory(fz_context *ctx, const unsigned char *data, size_t len);`
:	Open a stream reading from the data pointer.

`fz_stream *fz_open_buffer(fz_context *ctx, fz_buffer *buf);`
:	Open a stream reading from a buffer.

The basic stream operations you expect are available.

	int64_t fz_tell(fz_context *ctx, fz_stream *stm);
	void fz_seek(fz_context *ctx, fz_stream *stm, int64_t offset, int whence);
	size_t fz_read(fz_context *ctx, fz_stream *stm, unsigned char *data, size_t len);
	size_t fz_skip(fz_context *ctx, fz_stream *stm, size_t len);

`fz_buffer *fz_read_all(fz_context *ctx, fz_stream *stm, size_t initial);`
:	Read the remaining data into a new buffer.

`char *fz_read_line(fz_context *ctx, fz_stream *stm, char *buf, size_t n);`
:	Behaves like `fgets()`.

	int fz_read_byte(fz_context *ctx, fz_stream *stm);
	int fz_peek_byte(fz_context *ctx, fz_stream *stm);
	int fz_is_eof(fz_context *ctx, fz_stream *stm);

You can read binary data one integer at a time. The default is big endian, but LE versions are also provided.

	uint16_t fz_read_uint16(fz_context *ctx, fz_stream *stm);
	uint32_t fz_read_uint24(fz_context *ctx, fz_stream *stm);
	uint32_t fz_read_uint32(fz_context *ctx, fz_stream *stm);
	uint64_t fz_read_uint64(fz_context *ctx, fz_stream *stm);

	uint16_t fz_read_uint16_le(fz_context *ctx, fz_stream *stm);
	uint32_t fz_read_uint24_le(fz_context *ctx, fz_stream *stm);
	uint32_t fz_read_uint32_le(fz_context *ctx, fz_stream *stm);
	uint64_t fz_read_uint64_le(fz_context *ctx, fz_stream *stm);

	int16_t fz_read_int16(fz_context *ctx, fz_stream *stm);
	int32_t fz_read_int32(fz_context *ctx, fz_stream *stm);
	int64_t fz_read_int64(fz_context *ctx, fz_stream *stm);

	int16_t fz_read_int16_le(fz_context *ctx, fz_stream *stm);
	int32_t fz_read_int32_le(fz_context *ctx, fz_stream *stm);
	int64_t fz_read_int64_le(fz_context *ctx, fz_stream *stm);

Reading bit streams is also possible:

	unsigned int fz_read_bits(fz_context *ctx, fz_stream *stm, int n);
	unsigned int fz_read_rbits(fz_context *ctx, fz_stream *stm, int n);
	void fz_sync_bits(fz_context *ctx, fz_stream *stm);
	int fz_is_eof_bits(fz_context *ctx, fz_stream *stm);

## Filters

Various decoding, decompression, and decryption filters can be chained together.

	fz_stream *fz_open_null_filter(fz_context *ctx, fz_stream *chain, int len, int64_t offset);
	fz_stream *fz_open_arc4(fz_context *ctx, fz_stream *chain, unsigned char *key, unsigned keylen);
	fz_stream *fz_open_aesd(fz_context *ctx, fz_stream *chain, unsigned char *key, unsigned keylen);
	fz_stream *fz_open_a85d(fz_context *ctx, fz_stream *chain);
	fz_stream *fz_open_ahxd(fz_context *ctx, fz_stream *chain);
	fz_stream *fz_open_rld(fz_context *ctx, fz_stream *chain);
	fz_stream *fz_open_flated(fz_context *ctx, fz_stream *chain, int window_bits);

	fz_stream *fz_open_dctd(fz_context *ctx, fz_stream *chain,
		int color_transform,
		int invert_cmyk,
		int l2factor,
		fz_stream *jpegtables);

	fz_stream *fz_open_faxd(fz_context *ctx, fz_stream *chain,
		int k,
		int end_of_line,
		int encoded_byte_align,
		int columns,
		int rows,
		int end_of_block,
		int black_is_1);

	fz_stream *fz_open_lzwd(fz_context *ctx, fz_stream *chain,
		int early_change,
		int min_bits,
		int reverse_bits,
		int old_tiff);

	fz_stream *fz_open_predict(fz_context *ctx, fz_stream *chain,
		int predictor,
		int columns,
		int colors,
		int bpc);

## Output Streams

Output streams let us write data to a sink, usually a file on disk or a buffer. As with the input streams, output streams can be chained together to compress, encrypt, and encode data.

	typedef struct { internal } fz_output;

Because output may be buffered in the writer, we need a separate close function to ensure that an output stream is properly flushed and any end of data markers are written. This is separate to the drop function, which just frees data. If a writing operation has succeeded, you need to call close on the output stream before dropping it. If you encounter an error while writing data, you can just drop the stream directly, since we couldn't finish writing it and closing it properly would be irrelevant.

	void fz_close_output(fz_context *ctx, fz_output *out);
	void fz_drop_output(fz_context *ctx, fz_output *out);

Outputs can be created to write to files or buffers. You can also implement your own data sink by providing a state pointer and callback functions.

	fz_output *fz_new_output_with_path(fz_context *, const char *filename, int append);
	fz_output *fz_new_output_with_buffer(fz_context *ctx, fz_buffer *buf);

	fz_output *fz_new_output(fz_context *ctx,
		int buffer_size,
		void *state,
		void (*write)(fz_context *ctx, void *state, const void *data, size_t n),
		void (*close)(fz_context *ctx, void *state),
		void (*drop)(fz_context *ctx, void *state));

The usual suspects are available, as well as functions to write integers of various sizes and byte orders.

`void fz_seek_output(fz_context *ctx, fz_output *out, int64_t off, int whence);`
:	Seek to a location in the output. This is not available for all output types.

`int64_t fz_tell_output(fz_context *ctx, fz_output *out);`
:	Tell the current write location of the output stream.

	void fz_write_data(fz_context *ctx, fz_output *out, const void *data, size_t size);
	void fz_write_string(fz_context *ctx, fz_output *out, const char *s);
	void fz_write_byte(fz_context *ctx, fz_output *out, unsigned char x);
	void fz_write_rune(fz_context *ctx, fz_output *out, int rune);
	void fz_write_int16_be(fz_context *ctx, fz_output *out, int x);
	void fz_write_int16_le(fz_context *ctx, fz_output *out, int x);
	void fz_write_int32_be(fz_context *ctx, fz_output *out, int x);
	void fz_write_int32_le(fz_context *ctx, fz_output *out, int x);
	void fz_write_printf(fz_context *ctx, fz_output *out, const char *fmt, ...);
	void fz_write_vprintf(fz_context *ctx, fz_output *out, const char *fmt, va_list ap);
	void fz_write_base64(fz_context *ctx, fz_output *out, const unsigned char *data, size_t size, int newline);

Output streams can be chained together to add encryption, compression, and encoding. Note that these do not take ownership of the chained stream, they only write to it. For example, you can write a header, create a compression filter stream, write some data to the filter to compress the data, close the filter and then keep writing more data to the original stream.

	fz_output *fz_new_arc4_output(fz_context *ctx, fz_output *chain, unsigned char *key, size_t keylen);
	fz_output *fz_new_ascii85_output(fz_context *ctx, fz_output *chain);
	fz_output *fz_new_asciihex_output(fz_context *ctx, fz_output *chain);
	fz_output *fz_new_deflate_output(fz_context *ctx, fz_output *chain, int effort, int no_header);
	fz_output *fz_new_rle_output(fz_context *ctx, fz_output *chain);

## File Archives

The archive structure is a read-only collection of files. This is typically a Zip file or directory on disk, but other formats are also supported.

	typedef struct { internal } fz_archive;

	void fz_drop_archive(fz_context *ctx, fz_archive *arch);

	int fz_is_directory(fz_context *ctx, const char *path);

	fz_archive *fz_open_directory(fz_context *ctx, const char *path);
	fz_archive *fz_open_archive(fz_context *ctx, const char *filename);
	fz_archive *fz_open_archive_with_stream(fz_context *ctx, fz_stream *file);

	int fz_count_archive_entries(fz_context *ctx, fz_archive *arch);
	const char *fz_list_archive_entry(fz_context *ctx, fz_archive *arch, int idx);

	int fz_has_archive_entry(fz_context *ctx, fz_archive *arch, const char *name);
	fz_stream *fz_open_archive_entry(fz_context *ctx, fz_archive *arch, const char *name);
	fz_buffer *fz_read_archive_entry(fz_context *ctx, fz_archive *arch, const char *name);

We can also create new Zip archives.

	typedef struct { internal } fz_zip_writer;

	fz_zip_writer *fz_new_zip_writer(fz_context *ctx, const char *filename);
	fz_zip_writer *fz_new_zip_writer_with_output(fz_context *ctx, fz_output *out);
	void fz_write_zip_entry(fz_context *ctx, fz_zip_writer *zip, const char *name, fz_buffer *buf, int compress);
	void fz_close_zip_writer(fz_context *ctx, fz_zip_writer *zip);
	void fz_drop_zip_writer(fz_context *ctx, fz_zip_writer *zip);
