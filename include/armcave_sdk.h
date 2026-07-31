#pragma once

static inline int armcave_itoa(char *buf, int val)
{
    int pos = 0;
    unsigned int value;
    if (val < 0)
    {
        buf[pos++] = '-';
        value = 0U - (unsigned int)val;
    }
    else
    {
        value = (unsigned int)val;
    }
    char tmp[12];
    int tpos = 0;
    do
    {
        tmp[tpos++] = '0' + (char)(value % 10);
        value /= 10;
    } while (value > 0);
    while (tpos > 0)
        buf[pos++] = tmp[--tpos];
    buf[pos] = '\0';
    return pos;
}

static inline bool armcave_json_value(const char *json, int key,
                                      char *out, unsigned long out_size)
{
    if (!json || !out || out_size == 0)
        return false;

    char key_buf[16];
    armcave_itoa(key_buf, key);
    for (const char *p = json; *p; ++p)
    {
        while (*p && *p++ != '"')
        {
        }
        if (!*p)
            break;
        const char *a = key_buf;
        const char *b = p;
        while (*a && *b && *a == *b)
        {
            ++a;
            ++b;
        }
        if (!*a && *b == '"')
        {
            p = b + 1;
            while (*p && *p++ != '"')
            {
            }
            if (!*p)
                break;
            const char *value = p;
            while (*p && *p != '"')
                ++p;
            unsigned long length = 0;
            for (const char *c = value; c < p && length + 1 < out_size;
                 ++length)
                out[length] = *c++;
            out[length] = 0;
            return true;
        }
    }
    return false;
}

static inline const char *armcave_json_or_integer(
    const char *json, int key, char *label, unsigned long label_size,
    char *fallback)
{
    if (json && json[0] &&
        armcave_json_value(json, key, label, label_size))
        return label;
    armcave_itoa(fallback, key);
    return fallback;
}

static inline unsigned long armcave_text_length(const char *text)
{
    unsigned long length = 0;
    if (text)
        while (text[length])
            ++length;
    return length;
}

static inline bool armcave_text_equals(const char *a, unsigned long a_length,
                                       const char *b)
{
    unsigned long b_length = armcave_text_length(b);
    if (a_length != b_length)
        return false;
    for (unsigned long i = 0; i < a_length; ++i)
        if (!a || !b || a[i] != b[i])
            return false;
    return true;
}

static inline bool armcave_text_starts_with(const char *text, const char *prefix)
{
    if (!text || !prefix)
        return false;
    for (unsigned long i = 0; prefix[i]; ++i)
        if (!text[i] || text[i] != prefix[i])
            return false;
    return true;
}

static inline bool armcave_copy_text(char *out, unsigned long capacity,
                                     const char *text, unsigned long length)
{
    if (!out || !text || capacity == 0 || length >= capacity)
        return false;
    for (unsigned long i = 0; i < length; ++i)
        out[i] = text[i];
    out[length] = 0;
    return true;
}

static inline bool armcave_json_copy_or_integer(const char *json, int key,
                                                char *out,
                                                unsigned long out_size)
{
    if (!out || out_size == 0)
        return false;
    char label[64];
    char fallback[16];
    const char *text = armcave_json_or_integer(
        json, key, label, sizeof(label), fallback);
    return armcave_copy_text(out, out_size, text,
                             armcave_text_length(text));
}

struct armcave_asset_storage
{
    unsigned long words[4];
};

typedef const char *(*armcave_asset_open_fn)(const char *, void *);
typedef void (*armcave_asset_close_fn)(void *);

struct armcave_asset_reader
{
    armcave_asset_open_fn open;
    armcave_asset_close_fn close;
};

static inline const char *armcave_asset_load(
    const armcave_asset_reader &reader, const char *path,
    armcave_asset_storage &storage)
{
    storage = {};
    if (!path || !reader.open)
        return 0;
    return reader.open(path, &storage);
}

static inline void armcave_asset_release(
    const armcave_asset_reader &reader, armcave_asset_storage &storage)
{
    if (reader.close)
        reader.close(&storage);
    else
        storage = {};
}

typedef void *(*armcave_asset_binary_open_fn)(void *, const char *);
typedef long (*armcave_asset_binary_length_fn)(void *);
typedef long (*armcave_asset_binary_read_fn)(void *, void *, unsigned long);
typedef void (*armcave_asset_binary_close_fn)(void *);
typedef void *(*armcave_asset_binary_allocate_fn)(unsigned long);
typedef void (*armcave_asset_binary_deallocate_fn)(void *);

struct armcave_asset_binary_reader
{
    void *context;
    armcave_asset_binary_open_fn open;
    armcave_asset_binary_length_fn length;
    armcave_asset_binary_read_fn read;
    armcave_asset_binary_close_fn close;
    armcave_asset_binary_allocate_fn allocate;
    armcave_asset_binary_deallocate_fn deallocate;
};

static inline const char *armcave_asset_binary_load(
    const armcave_asset_binary_reader &reader, const char *path,
    unsigned long max_size, armcave_asset_storage &storage)
{
    storage = {};
    if (!path || !reader.open || !reader.length || !reader.read ||
        !reader.close || !reader.allocate || !reader.deallocate)
        return 0;
    void *asset = reader.open(reader.context, path);
    if (!asset)
        return 0;
    long signed_length = reader.length(asset);
    if (signed_length <= 0 ||
        (max_size && (unsigned long)signed_length > max_size))
    {
        reader.close(asset);
        return 0;
    }
    unsigned long length = (unsigned long)signed_length;
    char *data = (char *)reader.allocate(length + 1);
    if (!data)
    {
        reader.close(asset);
        return 0;
    }
    unsigned long total = 0;
    while (total < length)
    {
        long signed_count = reader.read(asset, data + total, length - total);
        if (signed_count <= 0 ||
            (unsigned long)signed_count > length - total)
        {
            reader.close(asset);
            reader.deallocate(data);
            return 0;
        }
        total += (unsigned long)signed_count;
    }
    reader.close(asset);
    data[total] = 0;
    storage.words[0] = (unsigned long)data;
    storage.words[1] = total;
    return data;
}

static inline unsigned long armcave_asset_binary_size(
    const armcave_asset_storage &storage)
{
    return storage.words[1];
}

static inline void armcave_asset_binary_release(
    const armcave_asset_binary_reader &reader, armcave_asset_storage &storage)
{
    if (storage.words[0] && reader.deallocate)
        reader.deallocate((void *)storage.words[0]);
    storage = {};
}

static inline bool armcave_load_rating_list(
    const armcave_asset_reader &reader, const char *path, int rating,
    char *out, unsigned long out_size)
{
    armcave_asset_storage storage = {};
    const char *json = armcave_asset_load(reader, path, storage);
    bool result = armcave_json_copy_or_integer(json, rating, out, out_size);
    armcave_asset_release(reader, storage);
    return result;
}

static inline bool armcave_load_rating_list(
    const armcave_asset_binary_reader &reader, const char *path, int rating,
    char *out, unsigned long out_size)
{
    armcave_asset_storage storage = {};
    const char *json = armcave_asset_binary_load(
        reader, path, 4UL * 1024UL * 1024UL, storage);
    bool result = armcave_json_copy_or_integer(json, rating, out, out_size);
    armcave_asset_binary_release(reader, storage);
    return result;
}

static inline bool armcave_append_text(char *out, unsigned long capacity,
                                       unsigned long &length,
                                       const char *text,
                                       unsigned long text_length)
{
    if (!out || !text || capacity == 0 || length >= capacity ||
        text_length > capacity - length - 1)
        return false;
    for (unsigned long i = 0; i < text_length; ++i)
        out[length++] = text[i];
    out[length] = 0;
    return true;
}

static inline bool armcave_is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

static inline void armcave_trim_span(const char *&begin, const char *&end)
{
    while (begin < end && armcave_is_space(*begin))
        ++begin;
    while (end > begin && armcave_is_space(end[-1]))
        --end;
}

static inline bool armcave_parse_u32(const char *&cursor, const char *end,
                                     int &value)
{
    while (cursor < end && armcave_is_space(*cursor))
        ++cursor;
    if (cursor == end || *cursor < '0' || *cursor > '9')
        return false;
    unsigned long result = 0;
    while (cursor < end && *cursor >= '0' && *cursor <= '9')
    {
        result = result * 10 + (unsigned long)(*cursor - '0');
        if (result > 2147483647UL)
            return false;
        ++cursor;
    }
    value = (int)result;
    return true;
}

static inline bool armcave_parse_float(const char *&cursor, const char *end,
                                       float &value)
{
    while (cursor < end && armcave_is_space(*cursor))
        ++cursor;
    bool negative = false;
    if (cursor < end && (*cursor == '-' || *cursor == '+'))
    {
        negative = *cursor == '-';
        ++cursor;
    }

    bool has_digit = false;
    unsigned int whole = 0;
    while (cursor < end && *cursor >= '0' && *cursor <= '9')
    {
        has_digit = true;
        unsigned int digit = (unsigned int)(*cursor++ - '0');
        if (whole > 100000U ||
            (whole == 100000U && digit > 0U))
            return false;
        whole = whole * 10U + digit;
    }

    unsigned int fraction = 0;
    unsigned int divisor = 1;
    if (cursor < end && *cursor == '.')
    {
        ++cursor;
        while (cursor < end && *cursor >= '0' && *cursor <= '9')
        {
            has_digit = true;
            unsigned int digit = (unsigned int)(*cursor++ - '0');
            if (divisor < 1000000U)
            {
                fraction = fraction * 10U + digit;
                divisor *= 10U;
            }
        }
    }
    if (!has_digit)
        return false;
    float result = (float)whole;
    if (divisor != 1U)
        result += (float)fraction / (float)divisor;
    value = negative ? -result : result;
    return true;
}

static inline bool armcave_valid_identifier(const char *begin,
                                             const char *end,
                                             unsigned long max_length)
{
    if (begin == end || (unsigned long)(end - begin) >= max_length)
        return false;
    char first = *begin;
    if (!((first >= 'a' && first <= 'z') ||
          (first >= 'A' && first <= 'Z') || first == '_'))
        return false;
    for (const char *cursor = begin + 1; cursor < end; ++cursor)
    {
        char c = *cursor;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_'))
            return false;
    }
    return true;
}

static inline bool armcave_safe_asset_path(const char *path)
{
    if (!path || !path[0] || path[0] == '/')
        return false;
    for (unsigned long i = 0; path[i]; ++i)
    {
        if (path[i] == '\\')
            return false;
        if (path[i] == '.' && path[i + 1] == '.' &&
            (i == 0 || path[i - 1] == '/') &&
            (path[i + 2] == 0 || path[i + 2] == '/'))
            return false;
    }
    return true;
}

static inline bool armcave_grow_capacity(unsigned long current,
                                          unsigned long required,
                                          unsigned long initial,
                                          unsigned long &result)
{
    result = current ? current : (initial ? initial : 1);
    while (result < required)
    {
        if (result > (~0UL) / 2)
            return false;
        result *= 2;
    }
    return true;
}
