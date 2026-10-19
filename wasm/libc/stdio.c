/* The standard I/O the compiler uses (fopen, freopen, fclose, fflush, setvbuf, fwrite, fputc, fputs) and the
 * low-level handles beneath (MSL Win32's open, write, lseek and close on its handle table), as MSL's: the same
 * buffering, flushing at the same points, and the same allocations (a FILE beyond the four static ones, each file's
 * 4 KiB buffer, each handle's entry, and the copy a text-mode write makes to put "\r" before each "\n"). Reading is
 * left out: the compiler reads its files through the Win32 calls. */
#include "libc.h"

enum file_kinds { closed_file, disk_file, console_file, string_file, unavailable_file };
enum open_modes { must_exist, create_if_necessary, create_or_truncate };
enum io_modes { io_read = 1, io_write = 2, io_read_write = 3, io_append = 4 };
enum io_states { neutral, writing, reading, rereading };
enum io_results { no_io_error, io_error, io_eof };

/* MSL Win32's open flags */
#define O_RDWR 0x0001
#define O_RDONLY 0x0002
#define O_WRONLY 0x0004
#define O_APPEND 0x0100
#define O_CREAT 0x0200
#define O_EXCL 0x0400
#define O_TRUNC 0x0800
#define O_BINARY 0x8000

#define SEEK_END 2

FileStruct* _HandleTable[NUM_HANDLES];
int _HandPtr;

/* --- the handles */

long lseek(int fildes, long offset, int whence)
{
    if (fildes >= NUM_HANDLES || !_HandleTable[fildes]) {
        LIBC_ERRNO = 3;
        return -1;
    }
    return SetFilePointer(_HandleTable[fildes]->handle, offset, NULL, whence);
}

static int open(const char* path, int flags)
{
    DWORD access, share, create;
    int h;

    if (((flags & O_RDONLY) != 0) + ((flags & O_RDWR) != 0) + ((flags & O_WRONLY) != 0) != 1) {
        return -1;
    }
    if ((flags & O_RDONLY) && (flags & O_TRUNC)) {
        return -1;
    }
    if ((flags & O_EXCL) && !(flags & O_CREAT)) {
        return -1;
    }
    h = GetHandle();
    if (h == -1) {
        return -1;
    }
    _HandleTable[h] = malloc(sizeof(FileStruct));
    if (!_HandleTable[h]) {
        return -1;
    }
    _HandleTable[h]->translate = (flags & O_BINARY) == 0;
    _HandleTable[h]->append = 0;
    share = 0;
    if (flags & O_RDONLY) {
        share = FILE_SHARE_READ;
        access = GENERIC_READ;
    } else if (flags & O_WRONLY) {
        access = GENERIC_WRITE;
    } else {
        access = GENERIC_READ | GENERIC_WRITE;
    }
    if (flags & O_CREAT) {
        create = CREATE_ALWAYS;
        if (!(flags & O_TRUNC) && (flags & O_APPEND)) {
            create = OPEN_ALWAYS;
        }
        if (flags & O_EXCL) {
            create = CREATE_NEW;
        }
    } else if (flags & O_TRUNC) {
        create = TRUNCATE_EXISTING;
    } else {
        create = OPEN_EXISTING;
    }
    _HandleTable[h]->handle = CreateFileA(path, access, share, NULL, create, 0, NULL);
    if (_HandleTable[h]->handle != INVALID_HANDLE_VALUE) {
        if (flags & O_APPEND) {
            lseek(h, 0, SEEK_END);
            _HandleTable[h]->append = 1;
        }
        return h;
    }
    free(_HandleTable[h]);
    _HandleTable[h] = 0;
    _doserrno = GetLastError();
    return -1;
}

/* writes COUNT bytes of BUF, "\n" as "\r\n" on a text-mode handle (through a copy) */
static int write(int fildes, const char* buf, int count)
{
    DWORD wrote;
    HANDLE h;
    char* copy = 0;
    int status;

    if (fildes >= NUM_HANDLES || !_HandleTable[fildes]) {
        LIBC_ERRNO = 3;
        return -1;
    }
    h = _HandleTable[fildes]->handle;
    if (_HandleTable[fildes]->translate) {
        int i, n = 0;
        char* p;
        for (i = 0; i < count; i++) {
            if (buf[i] == '\n') {
                n++;
            }
        }
        p = copy = malloc(count + n);
        for (i = 0; i < count; i++) {
            if (buf[i] == '\n') {
                *p++ = '\r';
            }
            *p++ = buf[i];
        }
        count += n;
        buf = copy;
    }
    if (_HandleTable[fildes]->append) {
        lseek(fildes, 0, SEEK_END);
    }
    status = WriteFile(h, buf, count, &wrote, NULL);
    if (copy) {
        free(copy);
    }
    if (status) {
        return wrote;
    }
    _doserrno = GetLastError();
    return 0;
}

static int close(int fildes)
{
    if (fildes >= NUM_HANDLES || !_HandleTable[fildes]) {
        return -1;
    }
    if (CloseHandle(_HandleTable[fildes]->handle)) {
        free(_HandleTable[fildes]);
        _HandleTable[fildes] = 0;
        return 0;
    }
    _doserrno = GetLastError();
    return -1;
}

/* --- a file's procedures */

static int position_file(__file_handle handle, fpos_t* position, int mode, __idle_proc idle)
{
    *position = lseek(handle, *position, mode);
    return *position == (fpos_t)-1 ? io_error : no_io_error;
}

static int read_file(__file_handle handle, unsigned char* buffer, size_t* count, __idle_proc idle)
{
    *count = 0;
    return io_eof;
}

static int write_file(__file_handle handle, unsigned char* buffer, size_t* count, __idle_proc idle)
{
    size_t n = write(handle, (char*)buffer, *count);

    if (n >= *count) {
        *count = n;
        return no_io_error;
    }
    *count = n;
    return io_error;
}

static int close_file(__file_handle handle)
{
    return close(handle) == -1 ? io_error : no_io_error;
}

/* --- the files */

/* the standard files' first buffers */
static unsigned char stdin_buff[256];
static unsigned char stdout_buff[256];
static unsigned char stderr_buff[256];

#define CONSOLE(handle, io_mode, buffer_mode, buffer, next) \
    { \
        (handle), {must_exist, (io_mode), (buffer_mode), console_file, 0, 0}, {neutral, 0, 0, 0}, 0, 0, 0, {0, 0}, \
            {0, 0}, 0, (buffer), 256, (buffer), 0, 0, 0, 0, 0, read_file, write_file, close_file, 0, (next), 0 \
    }

/* stdin, stdout, stderr and a closed one, the first a file opens; more are allocated */
FILE __files[4] = {
    CONSOLE(0, io_read, _IOLBF, stdin_buff, &__files[1]),
    CONSOLE(1, io_write, _IOLBF, stdout_buff, &__files[2]),
    CONSOLE(2, io_write, _IONBF, stderr_buff, &__files[3]),
};

void (*__stdio_exit)(void);

#define set_error(file) \
    do { \
        (file)->state.error = 1; \
        (file)->buffer_len = 0; \
    } while (0)

static void prep_buffer(FILE* file)
{
    file->buffer_ptr = file->buffer;
    file->buffer_len = file->buffer_size;
    file->buffer_len -= file->position & file->buffer_alignment;
    file->buffer_pos = file->position;
}

static int flush_buffer(FILE* file, size_t* bytes_flushed)
{
    size_t buffer_len = file->buffer_ptr - file->buffer;
    int result;

    if (buffer_len) {
        file->buffer_len = buffer_len;
        result = file->write_proc(file->handle, file->buffer, (size_t*)&file->buffer_len, file->idle_proc);
        if (bytes_flushed) {
            *bytes_flushed = file->buffer_len;
        }
        if (result) {
            return result;
        }
        file->position += file->buffer_len;
    }
    prep_buffer(file);
    return no_io_error;
}

static fpos_t file_tell(FILE* file)
{
    fpos_t position;

    if (!(file->mode.file_kind == disk_file || file->mode.file_kind == console_file) || file->state.error) {
        LIBC_ERRNO = 35;
        return -1;
    }
    if (file->state.io_state == neutral) {
        return file->position;
    }
    position = file->buffer_pos + (file->buffer_ptr - file->buffer);
    if (!file->mode.binary_io) {
        ptrdiff_t n = file->buffer_ptr - file->buffer;
        unsigned char* p = file->buffer;
        while (n--) {
            if (*p++ == '\n') {
                position++;
            }
        }
    }
    return position;
}

static int seek_end(FILE* file)
{
    fpos_t offset = 0;

    if (file->mode.file_kind != disk_file || file->state.error) {
        LIBC_ERRNO = 35;
        return -1;
    }
    if (file->state.io_state == writing && flush_buffer(file, 0) != no_io_error) {
        set_error(file);
        LIBC_ERRNO = 35;
        return -1;
    }
    if (file->position_proc && file->position_proc(file->handle, &offset, SEEK_END, file->idle_proc)) {
        set_error(file);
        LIBC_ERRNO = 35;
        return -1;
    }
    file->state.io_state = neutral;
    file->state.eof = 0;
    file->position = offset;
    file->buffer_len = 0;
    return 0;
}

int setvbuf(FILE* file, char* buff, int mode, size_t size)
{
    int kind = file->mode.file_kind;

    if (mode == _IONBF) {
        fflush(file);
    }
    if (file->state.io_state != neutral || kind == closed_file) {
        return -1;
    }
    if (mode != _IONBF && mode != _IOLBF && mode != _IOFBF) {
        return -1;
    }
    if (file->buffer && file->state.free_buffer) {
        free(file->buffer);
    }
    file->mode.buffer_mode = mode;
    file->state.free_buffer = 0;
    file->buffer = &file->char_buffer;
    file->buffer_ptr = &file->char_buffer;
    file->buffer_size = 1;
    file->buffer_len = 0;
    file->buffer_alignment = 0;
    if (mode == _IONBF || size < 1) {
        *file->buffer_ptr = 0;
        return 0;
    }
    if (!buff) {
        if (!(buff = malloc(size))) {
            return -1;
        }
        file->state.free_buffer = 1;
    }
    file->buffer = (unsigned char*)buff;
    file->buffer_ptr = file->buffer;
    file->buffer_size = size;
    file->buffer_alignment = 0;
    return 0;
}

static int flush_all(void)
{
    int result = 0;
    FILE* p;

    for (p = &__files[0]; p != 0; p = p->next_file_struct) {
        if (p->mode.file_kind != closed_file && fflush(p)) {
            result = -1;
        }
    }
    return result;
}

int fflush(FILE* file)
{
    fpos_t position;

    if (!file) {
        return flush_all();
    }
    if (file->state.error || file->mode.file_kind == closed_file) {
        return -1;
    }
    if (file->mode.io_mode == io_read) {
        return 0;
    }
    if (file->state.io_state >= rereading) {
        file->state.io_state = reading;
    }
    if (file->state.io_state == reading) {
        file->buffer_len = 0;
    }
    if (file->state.io_state != writing) {
        file->state.io_state = neutral;
        return 0;
    }
    if (file->mode.file_kind != disk_file || (position = file_tell(file)) < 0) {
        position = 0;
    }
    if (flush_buffer(file, 0)) {
        set_error(file);
        return -1;
    }
    file->state.io_state = neutral;
    file->position = position;
    file->buffer_len = 0;
    return 0;
}

int fclose(FILE* file)
{
    int flush_result, close_result;

    if (file == 0) {
        return -1;
    }
    if (file->mode.file_kind == closed_file) {
        return 0;
    }
    flush_result = fflush(file);
    close_result = file->close_proc(file->handle);
    file->mode.file_kind = closed_file;
    file->handle = 0;
    if (file->state.free_buffer) {
        free(file->buffer);
    }
    return flush_result || close_result ? -1 : 0;
}

static void close_all(void)
{
    FILE* p = &__files[0];
    FILE* last;

    while (p != 0) {
        if (p->mode.file_kind != closed_file) {
            fclose(p);
        }
        last = p;
        p = p->next_file_struct;
        if (last->is_dynamically_allocated) {
            free(last);
        } else {
            last->mode.file_kind = unavailable_file;
            if (p != 0 && p->is_dynamically_allocated) {
                last->next_file_struct = 0;
            }
        }
    }
}

void __stdio_atexit(void)
{
    __stdio_exit = close_all;
}

static FILE* find_unopened_file(void)
{
    FILE* p = __files[2].next_file_struct;
    FILE* last;

    while (p != 0) {
        if (p->mode.file_kind == closed_file) {
            return p;
        }
        last = p;
        p = p->next_file_struct;
    }
    if ((p = malloc(sizeof(FILE))) != 0) {
        memset(p, 0, sizeof(FILE));
        p->is_dynamically_allocated = 1;
        last->next_file_struct = p;
        return p;
    }
    return 0;
}

static int get_file_modes(const char* mode, __file_modes* modes)
{
    unsigned long mode_str;
    int open_mode, io_mode = 0;

    modes->file_kind = disk_file;
    modes->file_orientation = 0;
    modes->binary_io = 0;
    mode_str = *mode++;
    switch (mode_str) {
    case 'r':
        open_mode = must_exist;
        break;
    case 'w':
        open_mode = create_or_truncate;
        break;
    case 'a':
        open_mode = create_if_necessary;
        break;
    default:
        return 0;
    }
    modes->open_mode = open_mode;
    switch (*mode++) {
    case 'b':
        modes->binary_io = 1;
        if (*mode == '+') {
            mode_str = (mode_str << 8) | '+';
        }
        break;
    case '+':
        mode_str = (mode_str << 8) | '+';
        if (*mode == 'b') {
            modes->binary_io = 1;
        }
        break;
    }
    switch (mode_str) {
    case 'r':
        io_mode = io_read;
        break;
    case 'w':
        io_mode = io_write;
        break;
    case 'a':
        io_mode = io_write | io_append;
        break;
    case ('r' << 8) | '+':
    case ('w' << 8) | '+':
        io_mode = io_read_write;
        break;
    case ('a' << 8) | '+':
        io_mode = io_read_write | io_append;
        break;
    }
    modes->io_mode = io_mode;
    return 1;
}

static void init_file(FILE* file, __file_modes mode, char* buff, size_t size)
{
    file->handle = 0;
    file->mode = mode;
    file->state.io_state = neutral;
    file->state.free_buffer = 0;
    file->state.eof = 0;
    file->state.error = 0;
    file->position = 0;
    if (size) {
        setvbuf(file, buff, _IOFBF, size);
    } else {
        setvbuf(file, 0, _IONBF, 0);
    }
    file->buffer_ptr = file->buffer;
    file->buffer_len = 0;
    if (file->mode.file_kind == disk_file) {
        file->position_proc = position_file;
        file->read_proc = read_file;
        file->write_proc = write_file;
        file->close_proc = close_file;
    }
    file->idle_proc = 0;
}

static int open_file(const char* name, __file_modes mode, __file_handle* handle)
{
    int flags = 0;
    int h;

    if ((mode.io_mode & io_read_write) == io_read) {
        flags |= O_RDONLY;
    }
    if ((mode.io_mode & io_read_write) == io_write) {
        flags |= O_WRONLY;
    }
    if ((mode.io_mode & io_read_write) == io_read_write) {
        flags |= O_RDWR;
    }
    if (mode.io_mode & io_append) {
        flags |= O_APPEND;
    }
    if (mode.binary_io) {
        flags |= O_BINARY;
    }
    if (mode.open_mode == create_if_necessary) {
        flags |= O_CREAT;
    }
    if (mode.open_mode == create_or_truncate) {
        flags |= O_CREAT | O_TRUNC;
    }
    h = open(name, flags);
    if (h == -1) {
        return io_error;
    }
    *handle = h;
    return no_io_error;
}

FILE* freopen(const char* name, const char* mode, FILE* file)
{
    __file_modes modes;

    __stdio_atexit();
    if (!file) {
        return 0;
    }
    fclose(file);
    file->state.eof = 0;
    file->state.error = 0;
    if (!get_file_modes(mode, &modes)) {
        return 0;
    }
    init_file(file, modes, 0, 4096);
    if (open_file(name, modes, &file->handle)) {
        file->mode.file_kind = closed_file;
        if (file->state.free_buffer) {
            free(file->buffer);
        }
        return 0;
    }
    if (modes.io_mode & io_append) {
        seek_end(file);
    }
    return file;
}

FILE* fopen(const char* name, const char* mode)
{
    return freopen(name, mode, find_unopened_file());
}

static int put_char(int c, FILE* file)
{
    int kind = file->mode.file_kind;

    file->buffer_len = 0;
    if (file->state.error || kind == closed_file) {
        return -1;
    }
    if (kind == console_file) {
        __stdio_atexit();
    }
    if (file->state.io_state == neutral && (file->mode.io_mode & io_write)) {
        if ((file->mode.io_mode & io_append) && seek_end(file)) {
            return 0;
        }
        file->state.io_state = writing;
        prep_buffer(file);
    }
    if (file->state.io_state != writing) {
        set_error(file);
        return -1;
    }
    if (file->mode.buffer_mode == _IOFBF || file->buffer_ptr - file->buffer == file->buffer_size) {
        if (flush_buffer(file, 0)) {
            set_error(file);
            return -1;
        }
    }
    file->buffer_len--;
    *file->buffer_ptr++ = c;
    if (file->mode.buffer_mode != _IOFBF) {
        if (file->mode.buffer_mode == _IONBF || c == '\n') {
            if (flush_buffer(file, 0)) {
                set_error(file);
                return -1;
            }
        }
        file->buffer_len = 0;
    }
    return (unsigned char)c;
}

/* MSL's putc: into the buffer while it has room, else through put_char */
#define put(c, file) ((file)->buffer_len-- ? (int)(*(file)->buffer_ptr++ = (unsigned char)(c)) : put_char((c), (file)))

int fputc(int c, FILE* file)
{
    return put(c, file);
}

int fputs(const char* s, FILE* file)
{
    int c;

    while ((c = *s++) != 0) {
        if (put(c, file) == -1) {
            return -1;
        }
    }
    return 0;
}

size_t fwrite(const void* ptr, size_t memb_size, size_t num_memb, FILE* file)
{
    unsigned char* write_ptr;
    size_t num_bytes, bytes_to_go, bytes_written;
    int always_buffer;

    bytes_to_go = memb_size * num_memb;
    if (!bytes_to_go || file->state.error || file->mode.file_kind == closed_file) {
        return 0;
    }
    if (file->mode.file_kind == console_file) {
        __stdio_atexit();
    }
    always_buffer = !file->mode.binary_io || file->mode.file_kind == string_file ||
                    file->mode.buffer_mode == _IOFBF || file->mode.buffer_mode == _IOLBF;
    if (file->state.io_state == neutral && (file->mode.io_mode & io_write)) {
        if ((file->mode.io_mode & io_append) && seek_end(file)) {
            return 0;
        }
        file->state.io_state = writing;
        prep_buffer(file);
    }
    if (file->state.io_state != writing) {
        set_error(file);
        return 0;
    }

    write_ptr = (unsigned char*)ptr;
    bytes_written = 0;
    if (bytes_to_go && (file->buffer_ptr != file->buffer || always_buffer)) {
        file->buffer_len = file->buffer_size - (file->buffer_ptr - file->buffer);
        do {
            unsigned char* newline = 0;
            num_bytes = file->buffer_len;
            if (num_bytes > bytes_to_go) {
                num_bytes = bytes_to_go;
            }
            if (file->mode.buffer_mode == _IOLBF && num_bytes &&
                (newline = __memrchr(write_ptr, '\n', num_bytes)) != 0) {
                num_bytes = newline + 1 - write_ptr;
            }
            if (num_bytes) {
                memcpy(file->buffer_ptr, write_ptr, num_bytes);
                write_ptr += num_bytes;
                bytes_written += num_bytes;
                bytes_to_go -= num_bytes;
                file->buffer_ptr += num_bytes;
                file->buffer_len -= num_bytes;
            }
            if (!file->buffer_len && file->mode.file_kind == string_file) {
                bytes_written += bytes_to_go;
                break;
            }
            if (!file->buffer_len || newline != 0 || file->mode.buffer_mode == _IONBF) {
                if (flush_buffer(file, 0)) {
                    set_error(file);
                    bytes_to_go = 0;
                    break;
                }
            }
        } while (bytes_to_go && always_buffer);
    }

    if (bytes_to_go && !always_buffer) {
        unsigned char* save_buffer = file->buffer;
        size_t save_size = file->buffer_size;
        file->buffer = write_ptr;
        file->buffer_size = bytes_to_go;
        file->buffer_ptr = write_ptr + bytes_to_go;
        if (flush_buffer(file, &num_bytes) != no_io_error) {
            set_error(file);
        }
        bytes_written += num_bytes;
        file->buffer = save_buffer;
        file->buffer_size = save_size;
        prep_buffer(file);
        file->buffer_len = 0;
    }

    if (file->mode.buffer_mode != _IOFBF) {
        file->buffer_len = 0;
    }
    return (bytes_written + memb_size - 1) / memb_size;
}
