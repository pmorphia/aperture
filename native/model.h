/* Models for Aperture's backend: glTF 2.0 (.gltf with its buffers and images, or .glb) and
   Wavefront OBJ with MTL materials, loaded into a node tree of meshes and materials, with
   skins and animation clips for glTF.

   Included by backend.c after the mesh, texture and material code, whose static functions it
   uses; nothing here is a Flywheel function. Every offset read from a file is checked against
   the data it points into, so a damaged or hostile file fails with a message instead of
   reading out of bounds. */

/* --- The model ----------------------------------------------------------------------------- */
typedef struct {
    int mesh;     /* in A.meshes */
    int material; /* in A.materials, or 0 */
} Primitive;
typedef struct {
    int first, count; /* primitives */
} ModelMesh;
typedef struct {
    char *name;
    int parent, mesh, skin; /* -1 for none */
    bool has_matrix;        /* a fixed local matrix instead of translation, rotation, scale */
    float matrix[16];
    float t[3], r[4], s[3];           /* the pose */
    float rest_t[3], rest_r[4], rest_s[3]; /* as loaded */
    float world[16];
} Node;
typedef struct {
    int count;
    int *joints; /* nodes */
    float *inverse_bind; /* count matrices */
} Skin;
enum { PATH_TRANSLATION, PATH_ROTATION, PATH_SCALE };
enum { STEP_LINEAR, STEP_STEP, STEP_CUBIC };
typedef struct {
    int node, path, interpolation, count;
    float *times, *values; /* values: count (or 3 * count for cubic splines) of 3 or 4 floats */
} Channel;
typedef struct {
    char *name;
    Channel *channels;
    int channel_count;
    float length;
} Clip;
typedef struct Model {
    Node *nodes;
    int node_count;
    int *order; /* every node, parents before children */
    Primitive *primitives;
    int primitive_count;
    ModelMesh *meshes;
    int mesh_count;
    Skin *skins;
    int skin_count;
    Clip *clips;
    int clip_count;
    int *materials, material_count; /* owned */
    int *textures, texture_count;   /* owned */
    float low[3], high[3];          /* bounds in the rest pose */
    bool posed;                     /* worlds are current */
} Model;

static void model_release(Model *model) {
    if (!model)
        return;
    for (int i = 0; i < model->node_count; i++)
        free(model->nodes[i].name);
    for (int i = 0; i < model->skin_count; i++) {
        free(model->skins[i].joints);
        free(model->skins[i].inverse_bind);
    }
    for (int i = 0; i < model->clip_count; i++) {
        for (int k = 0; k < model->clips[i].channel_count; k++) {
            free(model->clips[i].channels[k].times);
            free(model->clips[i].channels[k].values);
        }
        free(model->clips[i].channels);
        free(model->clips[i].name);
    }
    free(model->nodes);
    free(model->order);
    free(model->primitives);
    free(model->meshes);
    free(model->skins);
    free(model->clips);
    free(model->materials);
    free(model->textures);
    free(model);
}
/* Frees the model's meshes, materials and textures on the graphics card, then the model. */
static void model_destroy(Model *model) {
    if (!model)
        return;
    for (int i = 0; i < model->primitive_count; i++)
        ap_mesh_free(model->primitives[i].mesh);
    for (int i = 0; i < model->material_count; i++)
        ap_material_free(model->materials[i]);
    for (int i = 0; i < model->texture_count; i++)
        ap_texture_free(model->textures[i]);
    model_release(model);
}
static char *copy_string(const char *text, size_t length) {
    char *out = malloc(length + 1);
    memcpy(out, text, length);
    out[length] = 0;
    return out;
}
static void *grow(void *array, int count, int *capacity, size_t size) {
    if (count < *capacity)
        return array;
    *capacity = *capacity ? *capacity * 2 : 16;
    return realloc(array, (size_t)*capacity * size);
}

/* --- Files --------------------------------------------------------------------------------- */
static unsigned char *read_file(const char *path, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    size_t capacity = 1 << 16, used = 0;
    unsigned char *data = malloc(capacity);
    size_t got;
    while ((got = fread(data + used, 1, capacity - used, file)) > 0) {
        used += got;
        if (used == capacity) {
            if (capacity >= ((size_t)1 << 31)) { /* 2 GB: not a model */
                free(data);
                fclose(file);
                return NULL;
            }
            capacity *= 2;
            data = realloc(data, capacity);
        }
    }
    fclose(file);
    *length = used;
    return data;
}
/* The directory part of a path, with its trailing separator ("" for none). */
static char *directory_of(const char *path) {
    const char *end = path;
    for (const char *c = path; *c; c++)
        if (*c == '/' || *c == '\\')
            end = c + 1;
    return copy_string(path, (size_t)(end - path));
}
/* A path relative to a directory, with %XX escapes (allowed in glTF URIs) decoded. */
static char *join_path(const char *directory, const char *relative) {
    size_t d = strlen(directory), r = strlen(relative);
    char *out = malloc(d + r + 1);
    memcpy(out, directory, d);
    size_t k = d;
    for (size_t i = 0; i < r; i++) {
        if (relative[i] == '%' && i + 2 < r && isxdigit((unsigned char)relative[i + 1]) &&
            isxdigit((unsigned char)relative[i + 2])) {
            char hex[3] = {relative[i + 1], relative[i + 2], 0};
            out[k++] = (char)strtol(hex, NULL, 16);
            i += 2;
        } else
            out[k++] = relative[i];
    }
    out[k] = 0;
    return out;
}
static int base64_value(unsigned char c) {
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+' || c == '-')
        return 62;
    if (c == '/' || c == '_')
        return 63;
    return -1;
}
/* Decodes the base64 after the comma of a data: URI. */
static unsigned char *data_uri(const char *uri, size_t *length) {
    const char *comma = strchr(uri, ',');
    if (!comma || !strstr(uri, ";base64"))
        return NULL;
    const char *text = comma + 1;
    size_t n = strlen(text);
    unsigned char *out = malloc(n / 4 * 3 + 3);
    size_t k = 0;
    unsigned bits = 0;
    int count = 0;
    for (size_t i = 0; i < n; i++) {
        int v = base64_value((unsigned char)text[i]);
        if (v < 0)
            continue; /* padding and whitespace */
        bits = bits << 6 | (unsigned)v;
        if (++count == 4) {
            out[k++] = (unsigned char)(bits >> 16);
            out[k++] = (unsigned char)(bits >> 8);
            out[k++] = (unsigned char)bits;
            bits = 0;
            count = 0;
        }
    }
    if (count == 2)
        out[k++] = (unsigned char)(bits >> 4);
    else if (count == 3) {
        out[k++] = (unsigned char)(bits >> 10);
        out[k++] = (unsigned char)(bits >> 2);
    }
    *length = k;
    return out;
}

/* --- JSON ---------------------------------------------------------------------------------- */
enum { JSON_NULL, JSON_FALSE, JSON_TRUE, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT };
typedef struct Json {
    int type, count;
    double number;
    char *string;       /* strings, and the keys of an object's members */
    struct Json *items; /* elements, or members (each with its key in `key`) */
    char *key;
} Json;
typedef struct {
    const char *at, *end;
    const char *error;
    int depth;
} JsonReader;
static void json_free(Json *j) {
    if (!j)
        return;
    for (int i = 0; i < j->count; i++)
        json_free(&j->items[i]);
    free(j->items);
    free(j->string);
    free(j->key);
}
static void json_space(JsonReader *r) {
    while (r->at < r->end && (*r->at == ' ' || *r->at == '\t' || *r->at == '\n' || *r->at == '\r'))
        r->at++;
}
static void put_utf8(char *out, size_t *k, unsigned code) {
    if (code < 0x80)
        out[(*k)++] = (char)code;
    else if (code < 0x800) {
        out[(*k)++] = (char)(0xC0 | code >> 6);
        out[(*k)++] = (char)(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out[(*k)++] = (char)(0xE0 | code >> 12);
        out[(*k)++] = (char)(0x80 | (code >> 6 & 0x3F));
        out[(*k)++] = (char)(0x80 | (code & 0x3F));
    } else {
        out[(*k)++] = (char)(0xF0 | code >> 18);
        out[(*k)++] = (char)(0x80 | (code >> 12 & 0x3F));
        out[(*k)++] = (char)(0x80 | (code >> 6 & 0x3F));
        out[(*k)++] = (char)(0x80 | (code & 0x3F));
    }
}
static char *json_string(JsonReader *r) {
    r->at++; /* the opening quote */
    const char *start = r->at;
    while (r->at < r->end && *r->at != '"')
        r->at += *r->at == '\\' && r->at + 1 < r->end ? 2 : 1;
    if (r->at >= r->end) {
        r->error = "a string that does not end";
        return NULL;
    }
    char *out = malloc((size_t)(r->at - start) + 1);
    size_t k = 0;
    for (const char *c = start; c < r->at; c++) {
        if (*c != '\\') {
            out[k++] = *c;
            continue;
        }
        c++;
        switch (*c) {
        case 'n': out[k++] = '\n'; break;
        case 't': out[k++] = '\t'; break;
        case 'r': out[k++] = '\r'; break;
        case 'b': out[k++] = '\b'; break;
        case 'f': out[k++] = '\f'; break;
        case 'u': {
            unsigned code = 0;
            int digits = 0;
            while (digits < 4 && c + 1 < r->at && isxdigit((unsigned char)c[1])) {
                c++;
                code = code * 16 + (unsigned)(isdigit((unsigned char)*c) ? *c - '0'
                                                                        : (*c | 32) - 'a' + 10);
                digits++;
            }
            if (code >= 0xD800 && code < 0xDC00 && c + 6 < r->at && c[1] == '\\' && c[2] == 'u') {
                unsigned low = (unsigned)strtol((char[5]){c[3], c[4], c[5], c[6], 0}, NULL, 16);
                if (low >= 0xDC00 && low < 0xE000) {
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    c += 6;
                }
            }
            /* a \u escape is at most 6 characters and its UTF-8 at most 4 bytes */
            put_utf8(out, &k, code);
            break;
        }
        default: out[k++] = *c;
        }
    }
    out[k] = 0;
    r->at++;
    return out;
}
static bool json_value(JsonReader *r, Json *out) {
    memset(out, 0, sizeof *out);
    json_space(r);
    if (r->at >= r->end) {
        r->error = "unexpected end";
        return false;
    }
    if (++r->depth > 64) {
        r->error = "nested too deeply";
        return false;
    }
    char c = *r->at;
    bool ok = true;
    if (c == '{' || c == '[') {
        bool object = c == '{';
        out->type = object ? JSON_OBJECT : JSON_ARRAY;
        r->at++;
        int capacity = 0;
        json_space(r);
        if (r->at < r->end && *r->at == (object ? '}' : ']'))
            r->at++;
        else
            for (;;) {
                char *key = NULL;
                if (object) {
                    json_space(r);
                    if (r->at >= r->end || *r->at != '"') {
                        r->error = "an object key that is not a string";
                        ok = false;
                        break;
                    }
                    key = json_string(r);
                    json_space(r);
                    if (!key || r->at >= r->end || *r->at != ':') {
                        free(key);
                        r->error = r->error ? r->error : "a missing ':'";
                        ok = false;
                        break;
                    }
                    r->at++;
                }
                out->items = grow(out->items, out->count, &capacity, sizeof(Json));
                Json *item = &out->items[out->count++];
                if (!json_value(r, item)) {
                    free(key);
                    ok = false;
                    break;
                }
                item->key = key;
                json_space(r);
                if (r->at < r->end && *r->at == ',') {
                    r->at++;
                    continue;
                }
                if (r->at < r->end && *r->at == (object ? '}' : ']')) {
                    r->at++;
                    break;
                }
                r->error = "a missing ',' or closing bracket";
                ok = false;
                break;
            }
    } else if (c == '"') {
        out->type = JSON_STRING;
        out->string = json_string(r);
        ok = out->string != NULL;
    } else if (c == 't' && r->end - r->at >= 4 && !memcmp(r->at, "true", 4)) {
        out->type = JSON_TRUE;
        r->at += 4;
    } else if (c == 'f' && r->end - r->at >= 5 && !memcmp(r->at, "false", 5)) {
        out->type = JSON_FALSE;
        r->at += 5;
    } else if (c == 'n' && r->end - r->at >= 4 && !memcmp(r->at, "null", 4)) {
        r->at += 4;
    } else if (c == '-' || (c >= '0' && c <= '9')) {
        char number[64];
        size_t n = 0;
        while (r->at < r->end && n < sizeof number - 1 && strchr("+-.eE0123456789", *r->at))
            number[n++] = *r->at++;
        number[n] = 0;
        out->type = JSON_NUMBER;
        out->number = strtod(number, NULL);
    } else {
        r->error = "an unexpected character";
        ok = false;
    }
    r->depth--;
    return ok;
}
static const Json *json_get(const Json *object, const char *key) {
    if (!object || object->type != JSON_OBJECT)
        return NULL;
    for (int i = 0; i < object->count; i++)
        if (object->items[i].key && !strcmp(object->items[i].key, key))
            return &object->items[i];
    return NULL;
}
static const Json *json_at(const Json *array, int index) {
    return array && array->type == JSON_ARRAY && index >= 0 && index < array->count
               ? &array->items[index]
               : NULL;
}
static int json_count(const Json *array) {
    return array && array->type == JSON_ARRAY ? array->count : 0;
}
static double json_number(const Json *object, const char *key, double fallback) {
    const Json *j = json_get(object, key);
    return j && j->type == JSON_NUMBER ? j->number : fallback;
}
static int json_int(const Json *object, const char *key, int fallback) {
    double n = json_number(object, key, fallback);
    return n >= -1 && n < 2147483647.0 ? (int)n : fallback;
}
static const char *json_text(const Json *object, const char *key) {
    const Json *j = json_get(object, key);
    return j && j->type == JSON_STRING ? j->string : NULL;
}
/* Reads up to `count` numbers of an array member into `out`; false when it is missing. */
static bool json_floats(const Json *object, const char *key, float *out, int count) {
    const Json *j = json_get(object, key);
    if (!j || j->type != JSON_ARRAY)
        return false;
    for (int i = 0; i < count && i < j->count; i++)
        if (j->items[i].type == JSON_NUMBER)
            out[i] = (float)j->items[i].number;
    return true;
}

/* --- glTF ---------------------------------------------------------------------------------- */
typedef struct {
    const unsigned char *data;
    size_t length;
} Bytes;
typedef struct {
    const char *path;
    char *directory;
    Json root;
    Bytes *buffers;
    unsigned char **owned_buffers;
    int buffer_count;
    Bytes glb_binary;
    int *image_textures; /* texture ids per glTF image, made when first used */
    bool *image_srgb;
    Model *model;
    char error[256];
} Gltf;
static bool gltf_fail(Gltf *g, const char *format, ...) {
    if (g->error[0])
        return false;
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(g->error, sizeof g->error, format, arguments);
    va_end(arguments);
    return false;
}
static const Json *gltf_array(Gltf *g, const char *name) {
    return json_get(&g->root, name);
}
/* The bytes of buffer view `index`, with its stride (0 when packed). */
static bool buffer_view(Gltf *g, int index, Bytes *out, int *stride) {
    const Json *view = json_at(gltf_array(g, "bufferViews"), index);
    if (!view)
        return gltf_fail(g, "buffer view %d does not exist", index);
    int buffer = json_int(view, "buffer", -1);
    double offset = json_number(view, "byteOffset", 0), length = json_number(view, "byteLength", -1);
    if (buffer < 0 || buffer >= g->buffer_count || !g->buffers[buffer].data)
        return gltf_fail(g, "buffer view %d refers to a missing buffer", index);
    if (offset < 0 || length < 0 || offset + length > (double)g->buffers[buffer].length)
        return gltf_fail(g, "buffer view %d reaches past the end of its buffer", index);
    out->data = g->buffers[buffer].data + (size_t)offset;
    out->length = (size_t)length;
    if (stride)
        *stride = json_int(view, "byteStride", 0);
    return true;
}
static int component_size(int type) {
    switch (type) {
    case 5120: case 5121: return 1; /* byte, unsigned byte */
    case 5122: case 5123: return 2; /* short, unsigned short */
    case 5125: case 5126: return 4; /* unsigned int, float */
    }
    return 0;
}
static int type_components(const char *type) {
    static const char *names[] = {"SCALAR", "VEC2", "VEC3", "VEC4", "MAT2", "MAT3", "MAT4"};
    static const int counts[] = {1, 2, 3, 4, 4, 9, 16};
    for (int i = 0; type && i < 7; i++)
        if (!strcmp(type, names[i]))
            return counts[i];
    return 0;
}
static float read_component(const unsigned char *p, int type, bool normalized) {
    switch (type) {
    case 5120: { signed char v; memcpy(&v, p, 1); return normalized ? fmaxf(v / 127.0f, -1) : v; }
    case 5121: return normalized ? p[0] / 255.0f : p[0];
    case 5122: { short v; memcpy(&v, p, 2); return normalized ? fmaxf(v / 32767.0f, -1) : v; }
    case 5123: { unsigned short v; memcpy(&v, p, 2); return normalized ? v / 65535.0f : v; }
    case 5125: { unsigned v; memcpy(&v, p, 4); return (float)v; }
    case 5126: { float v; memcpy(&v, p, 4); return v; }
    }
    return 0;
}
/* Reads accessor `index` as floats, `want` per element (extra components dropped, missing
   ones zero). Returns the element count (-1 on failure) and a malloc'd array in *out. */
static int read_accessor(Gltf *g, int index, int want, float **out) {
    *out = NULL;
    const Json *accessor = json_at(gltf_array(g, "accessors"), index);
    if (!accessor) {
        gltf_fail(g, "accessor %d does not exist", index);
        return -1;
    }
    int type = json_int(accessor, "componentType", 0), size = component_size(type);
    int components = type_components(json_text(accessor, "type"));
    double count_value = json_number(accessor, "count", -1);
    const Json *normalized_value = json_get(accessor, "normalized");
    bool normalized = normalized_value && normalized_value->type == JSON_TRUE;
    if (!size || !components || count_value < 0 || count_value > 100000000) {
        gltf_fail(g, "accessor %d has an unsupported type or count", index);
        return -1;
    }
    int count = (int)count_value;
    int view_index = json_int(accessor, "bufferView", -1);
    if (view_index < 0 && count > (1 << 22)) {
        gltf_fail(g, "accessor %d is too large", index);
        return -1;
    }
    float *values = calloc((size_t)count * want + 1, sizeof(float));
    if (view_index >= 0) {
        Bytes view;
        int stride;
        if (!buffer_view(g, view_index, &view, &stride)) {
            free(values);
            return -1;
        }
        size_t element = (size_t)size * components;
        if (stride <= 0)
            stride = (int)element;
        double offset = json_number(accessor, "byteOffset", 0);
        if (offset < 0 ||
            (count > 0 && offset + (double)stride * (count - 1) + element > (double)view.length)) {
            free(values);
            gltf_fail(g, "accessor %d reaches past the end of its buffer view", index);
            return -1;
        }
        /* Matrices of 1- and 2-byte components pad each column to 4 bytes. */
        int rows = components == 4 && strcmp(json_text(accessor, "type"), "MAT2") == 0   ? 2
                   : components == 9                                                    ? 3
                   : components == 16                                                   ? 4
                                                                                        : 0;
        for (int i = 0; i < count; i++) {
            const unsigned char *p = view.data + (size_t)offset + (size_t)stride * i;
            for (int c = 0; c < components && c < want; c++) {
                size_t at = (size_t)c * size;
                if (rows && size < 4) {
                    size_t column = (size_t)(rows * size + 3) / 4 * 4;
                    at = c / rows * column + (size_t)(c % rows) * size;
                }
                values[(size_t)i * want + c] = read_component(p + at, type, normalized);
            }
        }
    }
    /* Sparse accessors replace some elements. */
    const Json *sparse = json_get(accessor, "sparse");
    if (sparse) {
        int sparse_count = json_int(sparse, "count", 0);
        const Json *indices = json_get(sparse, "indices"), *replacements = json_get(sparse, "values");
        Bytes index_view, value_view;
        int index_type = json_int(indices, "componentType", 0), index_size = component_size(index_type);
        if (sparse_count < 0 || !index_size ||
            !buffer_view(g, json_int(indices, "bufferView", -1), &index_view, NULL) ||
            !buffer_view(g, json_int(replacements, "bufferView", -1), &value_view, NULL)) {
            free(values);
            gltf_fail(g, "accessor %d has broken sparse data", index);
            return -1;
        }
        double index_offset = json_number(indices, "byteOffset", 0);
        double value_offset = json_number(replacements, "byteOffset", 0);
        size_t element = (size_t)size * components;
        if (index_offset < 0 || value_offset < 0 ||
            index_offset + (double)index_size * sparse_count > (double)index_view.length ||
            value_offset + (double)element * sparse_count > (double)value_view.length) {
            free(values);
            gltf_fail(g, "accessor %d has sparse data past its buffer view", index);
            return -1;
        }
        for (int i = 0; i < sparse_count; i++) {
            float target = read_component(index_view.data + (size_t)index_offset + (size_t)i * index_size,
                                          index_type, false);
            if (target < 0 || target >= count)
                continue;
            const unsigned char *p = value_view.data + (size_t)value_offset + element * i;
            for (int c = 0; c < components && c < want; c++)
                values[(size_t)target * want + c] = read_component(p + (size_t)c * size, type, normalized);
        }
    }
    *out = values;
    return count;
}
/* Loads glTF image `index` as a texture (once), with the sampler's wrapping and filtering. */
static int gltf_image(Gltf *g, int texture_index, bool srgb) {
    (void)srgb;
    const Json *texture = json_at(gltf_array(g, "textures"), texture_index);
    if (!texture)
        return 0;
    int image_index = json_int(texture, "source", -1);
    const Json *extensions = json_get(texture, "extensions");
    if (image_index < 0 && extensions) {
        /* images in other formats name a fallback; there is none for WebP or Basis alone */
        image_index = json_int(json_get(extensions, "EXT_texture_webp"), "source", -1);
    }
    const Json *image = json_at(gltf_array(g, "images"), image_index);
    if (!image)
        return 0;
    if (g->image_textures[image_index])
        return g->image_textures[image_index];
    int width = 0, height = 0, channels = 0;
    unsigned char *pixels = NULL;
    const char *uri = json_text(image, "uri");
    if (uri && !strncmp(uri, "data:", 5)) {
        size_t length = 0;
        unsigned char *bytes = data_uri(uri, &length);
        if (bytes)
            pixels = stbi_load_from_memory(bytes, (int)length, &width, &height, &channels, 4);
        free(bytes);
    } else if (uri) {
        char *path = join_path(g->directory, uri);
        pixels = stbi_load(path, &width, &height, &channels, 4);
        free(path);
    } else {
        Bytes view;
        if (buffer_view(g, json_int(image, "bufferView", -1), &view, NULL))
            pixels = stbi_load_from_memory(view.data, (int)view.length, &width, &height, &channels, 4);
    }
    if (!pixels) {
        gltf_fail(g, "cannot read image %d (%s): %s", image_index, uri ? uri : "embedded",
                  stbi_failure_reason());
        return 0;
    }
    if (width > 16384 || height > 16384) {
        stbi_image_free(pixels);
        gltf_fail(g, "image %d is larger than 16384 pixels", image_index);
        return 0;
    }
    int id = texture_create(pixels, width, height, true);
    stbi_image_free(pixels);
    if (!id)
        return 0;
    const Json *sampler = json_at(gltf_array(g, "samplers"), json_int(texture, "sampler", -1));
    int wrap_s = json_int(sampler, "wrapS", 10497), wrap_t = json_int(sampler, "wrapT", 10497);
    int mag = json_int(sampler, "magFilter", GL_LINEAR), min = json_int(sampler, "minFilter", 0x2703);
    glBindTexture_(GL_TEXTURE_2D, A.textures[id].id);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                     wrap_s == 33071 ? GL_CLAMP_TO_EDGE : wrap_s == 33648 ? 0x8370 : GL_REPEAT);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                     wrap_t == 33071 ? GL_CLAMP_TO_EDGE : wrap_t == 33648 ? 0x8370 : GL_REPEAT);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag == GL_NEAREST ? GL_NEAREST : GL_LINEAR);
    bool mipmaps = min != GL_NEAREST && min != GL_LINEAR;
    if (mipmaps)
        glGenerateMipmap_(GL_TEXTURE_2D);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                     !mipmaps ? (min == GL_NEAREST ? GL_NEAREST : GL_LINEAR)
                     : min == 0x2700 || min == 0x2702 ? 0x2702 /* nearest texel, linear mip */
                                                      : 0x2703 /* GL_LINEAR_MIPMAP_LINEAR */);
    g->image_textures[image_index] = id;
    Model *m = g->model;
    int capacity = m->texture_count; /* grows by one each time; models have few images */
    m->textures = realloc(m->textures, (size_t)(capacity + 1) * sizeof(int));
    m->textures[m->texture_count++] = id;
    return id;
}
static int gltf_texture_of(Gltf *g, const Json *info, bool srgb) {
    if (!info)
        return 0;
    return gltf_image(g, json_int(info, "index", -1), srgb);
}
static bool gltf_materials(Gltf *g) {
    const Json *materials = gltf_array(g, "materials");
    Model *m = g->model;
    m->material_count = 0;
    m->materials = calloc((size_t)json_count(materials) + 1, sizeof(int));
    for (int i = 0; i < json_count(materials); i++) {
        const Json *source = json_at(materials, i);
        int id = material_create();
        if (!id)
            return false;
        m->materials[m->material_count++] = id;
        Material *out = &A.materials[id];
        const Json *pbr = json_get(source, "pbrMetallicRoughness");
        json_floats(pbr, "baseColorFactor", out->color, 4);
        out->metallic = (float)json_number(pbr, "metallicFactor", 1);
        out->roughness = (float)json_number(pbr, "roughnessFactor", 1);
        out->maps[MAP_BASE] = gltf_texture_of(g, json_get(pbr, "baseColorTexture"), true);
        out->maps[MAP_METAL_ROUGH] = gltf_texture_of(g, json_get(pbr, "metallicRoughnessTexture"), false);
        const Json *normal = json_get(source, "normalTexture");
        out->maps[MAP_NORMAL] = gltf_texture_of(g, normal, false);
        out->normal_scale = (float)json_number(normal, "scale", 1);
        const Json *occlusion = json_get(source, "occlusionTexture");
        out->maps[MAP_OCCLUSION] = gltf_texture_of(g, occlusion, false);
        out->occlusion_strength = (float)json_number(occlusion, "strength", 1);
        out->maps[MAP_EMISSIVE] = gltf_texture_of(g, json_get(source, "emissiveTexture"), true);
        json_floats(source, "emissiveFactor", out->emissive, 3);
        const Json *extensions = json_get(source, "extensions");
        float strength = (float)json_number(json_get(extensions, "KHR_materials_emissive_strength"),
                                            "emissiveStrength", 1);
        for (int c = 0; c < 3; c++)
            out->emissive[c] *= strength;
        if (json_get(extensions, "KHR_materials_unlit"))
            out->unlit = true;
        const char *mode = json_text(source, "alphaMode");
        if (mode && !strcmp(mode, "MASK"))
            out->alpha_cutoff = (float)json_number(source, "alphaCutoff", 0.5);
        out->blend = mode && !strcmp(mode, "BLEND");
        const Json *two_sided = json_get(source, "doubleSided");
        out->double_sided = two_sided && two_sided->type == JSON_TRUE;
        if (g->error[0])
            return false;
    }
    return true;
}
static unsigned char srgb_byte(float linear_value) {
    float c = to_srgb(fminf(fmaxf(linear_value, 0), 1));
    return (unsigned char)(c * 255 + 0.5f);
}
/* Builds one glTF primitive into an Aperture mesh. Returns its id, 0 when it is skipped (points
   and lines), -1 on failure. */
static int gltf_primitive(Gltf *g, const Json *primitive) {
    const Json *attributes = json_get(primitive, "attributes");
    int mode = json_int(primitive, "mode", 4);
    if (mode != 4 && mode != 5 && mode != 6)
        return 0;
    int position_index = json_int(attributes, "POSITION", -1);
    if (position_index < 0)
        return 0;
    float *positions = NULL, *normals = NULL, *uvs = NULL, *colors = NULL, *tangents = NULL,
          *joints = NULL, *weights = NULL, *indices = NULL;
    int count = read_accessor(g, position_index, 3, &positions);
    int result = -1;
    if (count < 0)
        goto done;
    const char *names[6] = {"NORMAL", "TEXCOORD_0", "COLOR_0", "TANGENT", "JOINTS_0", "WEIGHTS_0"};
    float **targets[6] = {&normals, &uvs, &colors, &tangents, &joints, &weights};
    int widths[6] = {3, 2, 4, 4, 4, 4};
    for (int a = 0; a < 6; a++) {
        int index = json_int(attributes, names[a], -1);
        if (index < 0)
            continue;
        int n = read_accessor(g, index, widths[a], targets[a]);
        if (n < 0)
            goto done;
        if (n != count) {
            gltf_fail(g, "a primitive's %s has %d values for %d positions", names[a], n, count);
            goto done;
        }
    }
    if (colors) { /* COLOR_0 may be RGB: then alpha is 1 */
        const Json *accessor = json_at(gltf_array(g, "accessors"), json_int(attributes, "COLOR_0", -1));
        if (type_components(json_text(accessor, "type")) == 3)
            for (int i = 0; i < count; i++)
                colors[i * 4 + 3] = 1;
    }
    ap_mesh_begin();
    A.build_tangents = tangents != NULL;
    for (int i = 0; i < count; i++) {
        const float *p = positions + i * 3;
        const float *n = normals ? normals + i * 3 : NULL;
        const float *t = uvs ? uvs + i * 2 : NULL;
        if (colors)
            for (int c = 0; c < 4; c++)
                A.build_color[c] = c < 3 ? srgb_byte(colors[i * 4 + c])
                                         : (unsigned char)(fminf(fmaxf(colors[i * 4 + 3], 0), 1) * 255);
        long v = ap_mesh_vertex(p[0], p[1], p[2], n ? n[0] : 0, n ? n[1] : 0, n ? n[2] : 0,
                                t ? t[0] : 0, t ? t[1] : 0);
        Vertex3 *out = &A.build_vertices[v];
        if (tangents)
            memcpy(&out->tx, tangents + i * 4, 4 * sizeof(float));
        if (joints && weights) {
            float sum = 0;
            for (int k = 0; k < 4; k++) {
                float j = joints[i * 4 + k], w = weights[i * 4 + k];
                out->joints[k] = (unsigned short)(j >= 0 && j < AP_MAX_JOINTS ? j : 0);
                out->weights[k] = w > 0 ? w : 0;
                sum += out->weights[k];
            }
            for (int k = 0; k < 4; k++)
                out->weights[k] = sum > 0 ? out->weights[k] / sum : (k == 0);
        }
    }
    int index_count = count;
    int index_accessor = json_int(primitive, "indices", -1);
    if (index_accessor >= 0 && (index_count = read_accessor(g, index_accessor, 1, &indices)) < 0)
        goto done;
#define AP_INDEX(i) (indices ? (long)indices[i] : (long)(i))
    for (int i = 0; i < index_count; i++)
        if (AP_INDEX(i) < 0 || AP_INDEX(i) >= count) {
            gltf_fail(g, "a primitive's index %ld is past its %d vertices", AP_INDEX(i), count);
            goto done;
        }
    if (mode == 4)
        for (int i = 0; i + 2 < index_count; i += 3)
            ap_mesh_triangle(AP_INDEX(i), AP_INDEX(i + 1), AP_INDEX(i + 2));
    else if (mode == 5)
        for (int i = 0; i + 2 < index_count; i++) {
            if (i % 2 == 0)
                ap_mesh_triangle(AP_INDEX(i), AP_INDEX(i + 1), AP_INDEX(i + 2));
            else
                ap_mesh_triangle(AP_INDEX(i + 1), AP_INDEX(i), AP_INDEX(i + 2));
        }
    else
        for (int i = 1; i + 1 < index_count; i++)
            ap_mesh_triangle(AP_INDEX(0), AP_INDEX(i), AP_INDEX(i + 1));
#undef AP_INDEX
    if (A.build_index_count < 3) {
        result = 0;
        goto done;
    }
    if (!normals) {
        /* Flat normals, as the specification asks: every triangle gets its own corners. */
        int triangles = A.build_index_count / 3;
        Vertex3 *flat = malloc((size_t)triangles * 3 * sizeof(Vertex3));
        for (int i = 0; i < triangles * 3; i++)
            flat[i] = A.build_vertices[A.build_indices[i]];
        for (int i = 0; i < triangles; i++) {
            Vertex3 *p = &flat[i * 3];
            float e1[3] = {p[1].x - p[0].x, p[1].y - p[0].y, p[1].z - p[0].z};
            float e2[3] = {p[2].x - p[0].x, p[2].y - p[0].y, p[2].z - p[0].z}, n[3];
            v3_cross(n, e1, e2);
            v3_normalize(n);
            for (int k = 0; k < 3; k++)
                p[k].nx = n[0], p[k].ny = n[1], p[k].nz = n[2];
        }
        A.build_vertex_count = 0;
        A.build_index_count = 0;
        for (int i = 0; i < triangles * 3; i++) {
            Vertex3 *v = &flat[i];
            long k = ap_mesh_vertex(v->x, v->y, v->z, v->nx, v->ny, v->nz, v->u, v->v);
            Vertex3 *out = &A.build_vertices[k];
            memcpy(&out->r, &v->r, 4);
            memcpy(&out->tx, &v->tx, 4 * sizeof(float));
            memcpy(out->joints, v->joints, sizeof out->joints);
            memcpy(out->weights, v->weights, sizeof out->weights);
        }
        for (int i = 0; i < triangles; i++)
            ap_mesh_triangle(i * 3, i * 3 + 1, i * 3 + 2);
        free(flat);
    }
    result = (int)ap_mesh_end();
    if (!result)
        result = -1;
done:
    A.build_color[0] = A.build_color[1] = A.build_color[2] = A.build_color[3] = 255;
    free(positions);
    free(normals);
    free(uvs);
    free(colors);
    free(tangents);
    free(joints);
    free(weights);
    free(indices);
    if (result < 0 && !g->error[0])
        gltf_fail(g, "cannot build a mesh");
    return result;
}
static bool gltf_meshes(Gltf *g) {
    const Json *meshes = gltf_array(g, "meshes");
    Model *m = g->model;
    m->mesh_count = json_count(meshes);
    m->meshes = calloc((size_t)m->mesh_count + 1, sizeof(ModelMesh));
    int capacity = 0;
    for (int i = 0; i < m->mesh_count; i++) {
        const Json *primitives = json_get(json_at(meshes, i), "primitives");
        m->meshes[i].first = m->primitive_count;
        for (int k = 0; k < json_count(primitives); k++) {
            const Json *primitive = json_at(primitives, k);
            int mesh = gltf_primitive(g, primitive);
            if (mesh < 0)
                return false;
            if (!mesh)
                continue;
            m->primitives = grow(m->primitives, m->primitive_count, &capacity, sizeof(Primitive));
            int material = json_int(primitive, "material", -1);
            m->primitives[m->primitive_count].mesh = mesh;
            m->primitives[m->primitive_count].material =
                material >= 0 && material < m->material_count ? m->materials[material] : 0;
            m->primitive_count++;
        }
        m->meshes[i].count = m->primitive_count - m->meshes[i].first;
    }
    return true;
}
static void quat_normalize(float *q) {
    float length = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (length < 1e-12f) {
        q[0] = q[1] = q[2] = 0, q[3] = 1;
        return;
    }
    for (int i = 0; i < 4; i++)
        q[i] /= length;
}
static bool gltf_nodes(Gltf *g) {
    const Json *nodes = gltf_array(g, "nodes");
    Model *m = g->model;
    m->node_count = json_count(nodes);
    m->nodes = calloc((size_t)m->node_count + 1, sizeof(Node));
    for (int i = 0; i < m->node_count; i++) {
        Node *n = &m->nodes[i];
        n->parent = -1;
        n->r[3] = 1;
        n->s[0] = n->s[1] = n->s[2] = 1;
    }
    for (int i = 0; i < m->node_count; i++) {
        const Json *source = json_at(nodes, i);
        Node *n = &m->nodes[i];
        const char *name = json_text(source, "name");
        n->name = copy_string(name ? name : "", name ? strlen(name) : 0);
        int mesh = json_int(source, "mesh", -1);
        n->mesh = mesh >= 0 && mesh < m->mesh_count ? mesh : -1;
        int skin = json_int(source, "skin", -1);
        n->skin = skin >= 0 && skin < json_count(gltf_array(g, "skins")) ? skin : -1;
        if (json_get(source, "matrix")) {
            m4_identity(n->matrix);
            json_floats(source, "matrix", n->matrix, 16);
            n->has_matrix = true;
        }
        json_floats(source, "translation", n->t, 3);
        json_floats(source, "rotation", n->r, 4);
        quat_normalize(n->r);
        json_floats(source, "scale", n->s, 3);
        memcpy(n->rest_t, n->t, sizeof n->t);
        memcpy(n->rest_r, n->r, sizeof n->r);
        memcpy(n->rest_s, n->s, sizeof n->s);
        const Json *children = json_get(source, "children");
        for (int k = 0; k < json_count(children); k++) {
            const Json *child = json_at(children, k);
            int c = child->type == JSON_NUMBER ? (int)child->number : -1;
            if (c < 0 || c >= m->node_count || c == i || m->nodes[c].parent >= 0)
                return gltf_fail(g, "node %d has a child that is not a node of its own", i);
            m->nodes[c].parent = i;
        }
    }
    /* Parents before children; a node that is its own ancestor is refused. */
    m->order = malloc(((size_t)m->node_count + 1) * sizeof(int));
    unsigned char *placed = calloc((size_t)m->node_count + 1, 1);
    int placed_count = 0;
    for (bool progress = true; progress && placed_count < m->node_count;) {
        progress = false;
        for (int i = 0; i < m->node_count; i++)
            if (!placed[i] && (m->nodes[i].parent < 0 || placed[m->nodes[i].parent])) {
                placed[i] = 1;
                m->order[placed_count++] = i;
                progress = true;
            }
    }
    free(placed);
    if (placed_count < m->node_count)
        return gltf_fail(g, "the nodes form a loop");
    return true;
}
/* Which nodes are drawn: those of the default scene (all roots when there is none). */
static void gltf_scene(Gltf *g, bool *shown) {
    Model *m = g->model;
    const Json *scenes = gltf_array(g, "scenes");
    const Json *scene = json_at(scenes, json_int(&g->root, "scene", 0));
    const Json *roots = json_get(scene, "nodes");
    if (!roots) {
        for (int i = 0; i < m->node_count; i++)
            shown[i] = true;
        return;
    }
    for (int k = 0; k < json_count(roots); k++) {
        const Json *root = json_at(roots, k);
        int r = root->type == JSON_NUMBER ? (int)root->number : -1;
        if (r >= 0 && r < m->node_count)
            shown[r] = true;
    }
    for (int k = 0; k < m->node_count; k++) {
        int i = m->order[k];
        if (m->nodes[i].parent >= 0 && shown[m->nodes[i].parent])
            shown[i] = true;
    }
}
static bool gltf_skins(Gltf *g) {
    const Json *skins = gltf_array(g, "skins");
    Model *m = g->model;
    m->skin_count = json_count(skins);
    m->skins = calloc((size_t)m->skin_count + 1, sizeof(Skin));
    for (int i = 0; i < m->skin_count; i++) {
        const Json *source = json_at(skins, i);
        const Json *joints = json_get(source, "joints");
        Skin *skin = &m->skins[i];
        skin->count = json_count(joints);
        if (skin->count > AP_MAX_JOINTS)
            return gltf_fail(g, "skin %d has %d joints (at most %d)", i, skin->count, AP_MAX_JOINTS);
        skin->joints = calloc((size_t)skin->count + 1, sizeof(int));
        skin->inverse_bind = calloc((size_t)skin->count * 16 + 1, sizeof(float));
        for (int k = 0; k < skin->count; k++) {
            const Json *joint = json_at(joints, k);
            int node = joint->type == JSON_NUMBER ? (int)joint->number : -1;
            if (node < 0 || node >= m->node_count)
                return gltf_fail(g, "skin %d has a joint that is not a node", i);
            skin->joints[k] = node;
            m4_identity(skin->inverse_bind + k * 16);
        }
        int matrices = json_int(source, "inverseBindMatrices", -1);
        if (matrices >= 0) {
            float *values;
            int n = read_accessor(g, matrices, 16, &values);
            if (n < 0)
                return false;
            memcpy(skin->inverse_bind, values, (size_t)(n < skin->count ? n : skin->count) * 16 * sizeof(float));
            free(values);
        }
    }
    return true;
}
static bool gltf_animations(Gltf *g) {
    const Json *animations = gltf_array(g, "animations");
    Model *m = g->model;
    m->clip_count = json_count(animations);
    m->clips = calloc((size_t)m->clip_count + 1, sizeof(Clip));
    for (int i = 0; i < m->clip_count; i++) {
        const Json *source = json_at(animations, i);
        Clip *clip = &m->clips[i];
        const char *name = json_text(source, "name");
        char fallback[32];
        snprintf(fallback, sizeof fallback, "animation %d", i);
        clip->name = copy_string(name ? name : fallback, strlen(name ? name : fallback));
        const Json *channels = json_get(source, "channels"), *samplers = json_get(source, "samplers");
        clip->channels = calloc((size_t)json_count(channels) + 1, sizeof(Channel));
        for (int k = 0; k < json_count(channels); k++) {
            const Json *channel = json_at(channels, k), *target = json_get(channel, "target");
            const char *path = json_text(target, "path");
            int node = json_int(target, "node", -1);
            int kind = !path                          ? -1
                       : !strcmp(path, "translation") ? PATH_TRANSLATION
                       : !strcmp(path, "rotation")    ? PATH_ROTATION
                       : !strcmp(path, "scale")       ? PATH_SCALE
                                                      : -1; /* morph weights are not supported */
            const Json *sampler = json_at(samplers, json_int(channel, "sampler", -1));
            if (kind < 0 || node < 0 || node >= m->node_count || !sampler)
                continue;
            Channel *out = &clip->channels[clip->channel_count];
            const char *interpolation = json_text(sampler, "interpolation");
            out->interpolation = !interpolation                      ? STEP_LINEAR
                                 : !strcmp(interpolation, "STEP")    ? STEP_STEP
                                 : !strcmp(interpolation, "CUBICSPLINE") ? STEP_CUBIC
                                                                     : STEP_LINEAR;
            out->node = node;
            out->path = kind;
            int width = kind == PATH_ROTATION ? 4 : 3;
            int times = read_accessor(g, json_int(sampler, "input", -1), 1, &out->times);
            if (times < 0)
                return false;
            int values = read_accessor(g, json_int(sampler, "output", -1), width, &out->values);
            if (values < 0) {
                free(out->times);
                return false;
            }
            int needed = out->interpolation == STEP_CUBIC ? times * 3 : times;
            if (times == 0 || values < needed) {
                free(out->times);
                free(out->values);
                continue;
            }
            out->count = times;
            if (out->times[times - 1] > clip->length)
                clip->length = out->times[times - 1];
            clip->channel_count++;
        }
    }
    return true;
}
static bool gltf_buffers(Gltf *g) {
    const Json *buffers = gltf_array(g, "buffers");
    g->buffer_count = json_count(buffers);
    g->buffers = calloc((size_t)g->buffer_count + 1, sizeof(Bytes));
    g->owned_buffers = calloc((size_t)g->buffer_count + 1, sizeof(unsigned char *));
    for (int i = 0; i < g->buffer_count; i++) {
        const Json *buffer = json_at(buffers, i);
        const char *uri = json_text(buffer, "uri");
        double declared = json_number(buffer, "byteLength", 0);
        size_t length = 0;
        unsigned char *data = NULL;
        if (!uri) {
            if (i != 0 || !g->glb_binary.data)
                return gltf_fail(g, "buffer %d has no data", i);
            g->buffers[i] = g->glb_binary;
        } else {
            if (!strncmp(uri, "data:", 5))
                data = data_uri(uri, &length);
            else {
                char *path = join_path(g->directory, uri);
                data = read_file(path, &length);
                free(path);
            }
            if (!data)
                return gltf_fail(g, "cannot read buffer %d (%s)", i,
                                 strncmp(uri, "data:", 5) ? uri : "embedded data");
            g->owned_buffers[i] = data;
            g->buffers[i].data = data;
            g->buffers[i].length = length;
        }
        if (declared > (double)g->buffers[i].length)
            return gltf_fail(g, "buffer %d is shorter than it says", i);
    }
    return true;
}
static unsigned read_u32(const unsigned char *p) {
    return (unsigned)p[0] | (unsigned)p[1] << 8 | (unsigned)p[2] << 16 | (unsigned)p[3] << 24;
}
static Model *load_gltf(const char *path, char *error, size_t error_size) {
    Gltf g;
    memset(&g, 0, sizeof g);
    g.path = path;
    size_t length = 0;
    unsigned char *file = read_file(path, &length);
    if (!file) {
        snprintf(error, error_size, "cannot open model '%s'", path);
        return NULL;
    }
    const char *json_start = (const char *)file;
    size_t json_length = length;
    if (length >= 12 && !memcmp(file, "glTF", 4)) {
        /* GLB: a header, then a JSON chunk and an optional binary chunk */
        size_t total = read_u32(file + 8);
        if (total > length || length < 20) {
            free(file);
            snprintf(error, error_size, "model '%s' is a cut-off .glb file", path);
            return NULL;
        }
        size_t at = 12;
        json_start = NULL;
        while (at + 8 <= total) {
            size_t chunk = read_u32(file + at), kind = read_u32(file + at + 4);
            if (chunk > total - at - 8)
                break;
            if (kind == 0x4E4F534A && !json_start) {
                json_start = (const char *)file + at + 8;
                json_length = chunk;
            } else if (kind == 0x004E4942 && !g.glb_binary.data) {
                g.glb_binary.data = file + at + 8;
                g.glb_binary.length = chunk;
            }
            at += 8 + (chunk + 3) / 4 * 4;
        }
        if (!json_start) {
            free(file);
            snprintf(error, error_size, "model '%s' has no JSON chunk", path);
            return NULL;
        }
    }
    JsonReader reader = {json_start, json_start + json_length, NULL, 0};
    if (json_length >= 3 && !memcmp(json_start, "\xEF\xBB\xBF", 3))
        reader.at += 3;
    if (!json_value(&reader, &g.root) || g.root.type != JSON_OBJECT) {
        snprintf(error, error_size, "model '%s' is not valid glTF JSON (%s)", path,
                 reader.error ? reader.error : "not an object");
        json_free(&g.root);
        free(file);
        return NULL;
    }
    g.directory = directory_of(path);
    g.model = calloc(1, sizeof(Model));
    g.image_textures = calloc((size_t)json_count(gltf_array(&g, "images")) + 1, sizeof(int));
    const Json *asset = json_get(&g.root, "asset");
    const char *version = json_text(asset, "version");
    bool ok = true;
    if (version && version[0] != '2')
        ok = gltf_fail(&g, "it is glTF %s; only glTF 2.0 is supported", version);
    const Json *required = json_get(&g.root, "extensionsRequired");
    for (int i = 0; ok && i < json_count(required); i++) {
        const char *name = json_at(required, i)->string;
        if (name && strcmp(name, "KHR_materials_unlit") && strcmp(name, "KHR_materials_emissive_strength"))
            ok = gltf_fail(&g, "it needs the unsupported extension %s", name);
    }
    ok = ok && gltf_buffers(&g) && gltf_materials(&g) && gltf_meshes(&g) && gltf_nodes(&g) &&
         gltf_skins(&g) && gltf_animations(&g);
    Model *model = g.model;
    if (ok) {
        /* Nodes outside the default scene are not drawn: they lose their meshes. */
        bool *shown = calloc((size_t)model->node_count + 1, sizeof(bool));
        gltf_scene(&g, shown);
        for (int i = 0; i < model->node_count; i++)
            if (!shown[i])
                model->nodes[i].mesh = -1;
        free(shown);
    }
    json_free(&g.root);
    for (int i = 0; i < g.buffer_count; i++)
        free(g.owned_buffers[i]);
    free(g.owned_buffers);
    free(g.buffers);
    free(g.image_textures);
    free(g.directory);
    free(file);
    if (!ok) {
        snprintf(error, error_size, "cannot load model '%s': %s", path,
                 g.error[0] ? g.error : "unknown error");
        model_destroy(model);
        return NULL;
    }
    return model;
}

/* --- OBJ with MTL materials ---------------------------------------------------------------- */
typedef struct {
    char name[128];
    int material;
} NamedMaterial;
static char *trim(char *text) {
    while (*text == ' ' || *text == '\t')
        text++;
    size_t n = strlen(text);
    while (n && (text[n - 1] == '\n' || text[n - 1] == '\r' || text[n - 1] == ' ' || text[n - 1] == '\t'))
        text[--n] = 0;
    return text;
}
/* The file name at the end of a map_ statement, after options like -bm 0.5. */
static char *map_file(char *rest) {
    char *last = rest;
    for (char *c = rest; *c;) {
        while (*c == ' ' || *c == '\t')
            c++;
        if (*c == '-') {
            /* an option and its numbers */
            while (*c && *c != ' ' && *c != '\t')
                c++;
            for (;;) {
                while (*c == ' ' || *c == '\t')
                    c++;
                char *end;
                strtod(c, &end);
                if (end == c || (*end && *end != ' ' && *end != '\t'))
                    break;
                c = end;
            }
            last = c;
            continue;
        }
        return trim(c);
    }
    return trim(last);
}
static int load_map(Model *model, const char *directory, char *rest, bool owned_by_model) {
    (void)owned_by_model;
    char *path = join_path(directory, map_file(rest));
    for (char *c = path; *c; c++)
        if (*c == '\\')
            *c = '/';
    int width, height, channels;
    unsigned char *pixels = stbi_load(path, &width, &height, &channels, 4);
    free(path);
    if (!pixels)
        return 0; /* a missing texture leaves the material untextured */
    int id = width <= 16384 && height <= 16384 ? texture_create(pixels, width, height, true) : 0;
    stbi_image_free(pixels);
    if (!id)
        return 0;
    glBindTexture_(GL_TEXTURE_2D, A.textures[id].id);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap_(GL_TEXTURE_2D);
    glTexParameteri_(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, 0x2703);
    model->textures = realloc(model->textures, (size_t)(model->texture_count + 1) * sizeof(int));
    model->textures[model->texture_count++] = id;
    return id;
}
/* Reads an MTL file's materials into the model (Kd, d, Ks/Ns, Ke, the PBR extension's Pr and
   Pm, and the map_ textures). */
static void load_mtl(Model *model, const char *path, NamedMaterial **named, int *named_count) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return;
    char *directory = directory_of(path);
    char line[1024];
    Material *m = NULL;
    int capacity = *named_count;
    float specular = -1, shininess = -1;
    bool roughness_given = false;
#define AP_FINISH_MTL()                                                                            \
    if (m && !roughness_given && shininess >= 0) /* Phong exponent to roughness */               \
        m->roughness = fminf(fmaxf(sqrtf(2 / (shininess + 2)), 0.04f), 1);                         \
    (void)specular;
    while (fgets(line, sizeof line, file)) {
        char *text = trim(line);
        char keyword[32] = "";
        int used = 0;
        if (sscanf(text, "%31s%n", keyword, &used) != 1 || keyword[0] == '#')
            continue;
        char *rest = trim(text + used);
        if (!strcmp(keyword, "newmtl")) {
            AP_FINISH_MTL();
            int id = material_create();
            if (!id)
                break;
            model->materials = realloc(model->materials, (size_t)(model->material_count + 1) * sizeof(int));
            model->materials[model->material_count++] = id;
            m = &A.materials[id];
            m->roughness = 0.8f;
            specular = shininess = -1;
            roughness_given = false;
            capacity = *named_count; /* realloc to exact size each time: MTL files are small */
            *named = realloc(*named, (size_t)(capacity + 1) * sizeof(NamedMaterial));
            snprintf((*named)[*named_count].name, sizeof(*named)[0].name, "%s", rest);
            (*named)[(*named_count)++].material = id;
            continue;
        }
        if (!m)
            continue;
        float a = 0, b = 0, c = 0;
        int n = sscanf(rest, "%f %f %f", &a, &b, &c);
        if (!strcmp(keyword, "Kd") && n >= 1) {
            m->color[0] = a, m->color[1] = n >= 3 ? b : a, m->color[2] = n >= 3 ? c : a;
            /* MTL colors are written as they look: treat them as sRGB */
            for (int i = 0; i < 3; i++)
                m->color[i] = to_linear(m->color[i]);
        } else if (!strcmp(keyword, "d") && n >= 1)
            m->color[3] = a;
        else if (!strcmp(keyword, "Tr") && n >= 1)
            m->color[3] = 1 - a;
        else if (!strcmp(keyword, "Ns") && n >= 1)
            shininess = a;
        else if (!strcmp(keyword, "Ks") && n >= 1)
            specular = a;
        else if (!strcmp(keyword, "Ke") && n >= 1) {
            m->emissive[0] = to_linear(a), m->emissive[1] = to_linear(n >= 3 ? b : a);
            m->emissive[2] = to_linear(n >= 3 ? c : a);
        } else if (!strcmp(keyword, "Pr") && n >= 1) {
            m->roughness = fminf(fmaxf(a, 0), 1);
            roughness_given = true;
        } else if (!strcmp(keyword, "Pm") && n >= 1)
            m->metallic = fminf(fmaxf(a, 0), 1);
        else if (!strcmp(keyword, "map_Kd"))
            m->maps[MAP_BASE] = load_map(model, directory, rest, true);
        else if (!strcmp(keyword, "map_Bump") || !strcmp(keyword, "map_bump") ||
                 !strcmp(keyword, "bump") || !strcmp(keyword, "norm") || !strcmp(keyword, "map_Kn"))
            m->maps[MAP_NORMAL] = load_map(model, directory, rest, true);
        else if (!strcmp(keyword, "map_Ke"))
            m->maps[MAP_EMISSIVE] = load_map(model, directory, rest, true);
        else if (!strcmp(keyword, "map_Pr") || !strcmp(keyword, "map_Pm") ||
                 !strcmp(keyword, "map_ORM") || !strcmp(keyword, "map_RMA")) {
            /* one packed texture: green roughness, blue metallic (glTF's layout) */
            if (!m->maps[MAP_METAL_ROUGH])
                m->maps[MAP_METAL_ROUGH] = load_map(model, directory, rest, true);
        } else if (!strcmp(keyword, "map_d") && m->alpha_cutoff == 0)
            m->alpha_cutoff = 0.5f;
        if (m->color[3] < 1)
            m->blend = true;
    }
    AP_FINISH_MTL();
#undef AP_FINISH_MTL
    fclose(file);
    free(directory);
}
/* An OBJ file as a model: one node whose mesh has one primitive per material used. */
static Model *load_obj(const char *path, char *error, size_t error_size) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        snprintf(error, error_size, "cannot open model '%s'", path);
        return NULL;
    }
    Model *model = calloc(1, sizeof(Model));
    char *directory = directory_of(path);
    NamedMaterial *named = NULL;
    int named_count = 0;
    float *positions = NULL, *uvs = NULL, *normals = NULL;
    int position_count = 0, uv_count = 0, normal_count = 0;
    int position_capacity = 0, uv_capacity = 0, normal_capacity = 0;
    /* Faces are collected per material as corner triples, then built one material at a time. */
    typedef struct {
        int material;
        long *corners; /* position, uv, normal indices (1-based, 0 for none) */
        int count, capacity;
        bool missing_normals;
    } Group;
    Group *groups = NULL;
    int group_count = 0, group_capacity = 0, current = -1;
    bool bad = false;
    char line[4096];
    while (fgets(line, sizeof line, file)) {
        char *text = line;
        while (*text == ' ' || *text == '\t')
            text++;
        if (text[0] == 'v' && text[1] == ' ') {
            positions = grow(positions, position_count, &position_capacity, 3 * sizeof(float));
            float *p = positions + position_count++ * 3;
            p[0] = p[1] = p[2] = 0;
            sscanf(text + 2, "%f %f %f", &p[0], &p[1], &p[2]);
        } else if (text[0] == 'v' && text[1] == 't') {
            uvs = grow(uvs, uv_count, &uv_capacity, 2 * sizeof(float));
            float *p = uvs + uv_count++ * 2;
            p[0] = p[1] = 0;
            sscanf(text + 3, "%f %f", &p[0], &p[1]);
        } else if (text[0] == 'v' && text[1] == 'n') {
            normals = grow(normals, normal_count, &normal_capacity, 3 * sizeof(float));
            float *p = normals + normal_count++ * 3;
            p[0] = p[1] = p[2] = 0;
            sscanf(text + 3, "%f %f %f", &p[0], &p[1], &p[2]);
        } else if (!strncmp(text, "mtllib", 6) && (text[6] == ' ' || text[6] == '\t')) {
            char *library = join_path(directory, trim(text + 7));
            load_mtl(model, library, &named, &named_count);
            free(library);
        } else if (!strncmp(text, "usemtl", 6) && (text[6] == ' ' || text[6] == '\t')) {
            char *name = trim(text + 7);
            int material = 0;
            for (int i = 0; i < named_count; i++)
                if (!strcmp(named[i].name, name))
                    material = named[i].material;
            current = -1;
            for (int i = 0; i < group_count; i++)
                if (groups[i].material == material)
                    current = i;
            if (current < 0) {
                groups = grow(groups, group_count, &group_capacity, sizeof(Group));
                memset(&groups[group_count], 0, sizeof(Group));
                groups[group_count].material = material;
                current = group_count++;
            }
        } else if (text[0] == 'f' && (text[1] == ' ' || text[1] == '\t')) {
            if (current < 0) {
                groups = grow(groups, group_count, &group_capacity, sizeof(Group));
                memset(&groups[group_count], 0, sizeof(Group));
                current = group_count++;
            }
            Group *group = &groups[current];
            long corners[64][3];
            int count = 0;
            char *cursor = text + 2;
            while (count < 64) {
                while (*cursor == ' ' || *cursor == '\t')
                    cursor++;
                if (!*cursor || *cursor == '\n' || *cursor == '\r')
                    break;
                long v = strtol(cursor, &cursor, 10), vt = 0, vn = 0;
                if (*cursor == '/') {
                    cursor++;
                    if (*cursor != '/')
                        vt = strtol(cursor, &cursor, 10);
                    if (*cursor == '/') {
                        cursor++;
                        vn = strtol(cursor, &cursor, 10);
                    }
                }
                while (*cursor && *cursor != ' ' && *cursor != '\t' && *cursor != '\n' && *cursor != '\r')
                    cursor++;
                if (v < 0)
                    v += position_count + 1;
                if (vt < 0)
                    vt += uv_count + 1;
                if (vn < 0)
                    vn += normal_count + 1;
                if (v < 1 || v > position_count || vt < 0 || vt > uv_count || vn < 0 || vn > normal_count) {
                    bad = true;
                    break;
                }
                if (!vn)
                    group->missing_normals = true;
                corners[count][0] = v, corners[count][1] = vt, corners[count][2] = vn;
                count++;
            }
            for (int i = 1; i + 1 < count; i++) {
                int need = group->count + 9;
                if (need > group->capacity) {
                    group->capacity = need * 2;
                    group->corners = realloc(group->corners, (size_t)group->capacity * sizeof(long));
                }
                int pick[3] = {0, i, i + 1};
                for (int k = 0; k < 3; k++)
                    for (int c = 0; c < 3; c++)
                        group->corners[group->count++] = corners[pick[k]][c];
            }
        }
    }
    fclose(file);
    bool ok = !bad;
    int capacity = 0;
    for (int i = 0; ok && i < group_count; i++) {
        Group *group = &groups[i];
        if (group->count < 9)
            continue;
        ap_mesh_begin();
        for (int k = 0; k < group->count; k += 3) {
            const float *p = positions + (group->corners[k] - 1) * 3;
            const float *t = group->corners[k + 1] ? uvs + (group->corners[k + 1] - 1) * 2 : NULL;
            const float *n = group->corners[k + 2] ? normals + (group->corners[k + 2] - 1) * 3 : NULL;
            ap_mesh_vertex(p[0], p[1], p[2], n ? n[0] : 0, n ? n[1] : 0, n ? n[2] : 0,
                           t ? t[0] : 0, t ? 1 - t[1] : 0);
        }
        for (int k = 0; k + 2 < A.build_vertex_count; k += 3)
            ap_mesh_triangle(k, k + 1, k + 2);
        if (group->missing_normals)
            ap_mesh_smooth(); /* corners are not shared: flat normals */
        int mesh = (int)ap_mesh_end();
        if (!mesh) {
            ok = false;
            break;
        }
        model->primitives = grow(model->primitives, model->primitive_count, &capacity, sizeof(Primitive));
        model->primitives[model->primitive_count].mesh = mesh;
        model->primitives[model->primitive_count].material = group->material;
        model->primitive_count++;
    }
    for (int i = 0; i < group_count; i++)
        free(groups[i].corners);
    free(groups);
    free(positions);
    free(uvs);
    free(normals);
    free(named);
    free(directory);
    if (ok && model->primitive_count == 0) {
        snprintf(error, error_size, "model '%s' has no faces", path);
        ok = false;
    } else if (!ok)
        snprintf(error, error_size, bad ? "model '%s' has a face that refers to missing data"
                                        : "cannot build model '%s'",
                 path);
    if (!ok) {
        model_destroy(model);
        return NULL;
    }
    model->mesh_count = 1;
    model->meshes = calloc(1, sizeof(ModelMesh));
    model->meshes[0].count = model->primitive_count;
    model->node_count = 1;
    model->nodes = calloc(1, sizeof(Node));
    model->nodes[0].name = copy_string("", 0);
    model->nodes[0].parent = model->nodes[0].skin = -1;
    model->nodes[0].r[3] = model->nodes[0].rest_r[3] = 1;
    for (int c = 0; c < 3; c++)
        model->nodes[0].s[c] = model->nodes[0].rest_s[c] = 1;
    model->order = calloc(1, sizeof(int));
    return model;
}

/* --- Poses ---------------------------------------------------------------------------------- */
static void trs_matrix(float *m, const float *t, const float *r, const float *s) {
    float x = r[0], y = r[1], z = r[2], w = r[3];
    float rotation[9] = {1 - 2 * (y * y + z * z), 2 * (x * y + z * w),     2 * (x * z - y * w),
                         2 * (x * y - z * w),     1 - 2 * (x * x + z * z), 2 * (y * z + x * w),
                         2 * (x * z + y * w),     2 * (y * z - x * w),     1 - 2 * (x * x + y * y)};
    for (int c = 0; c < 3; c++) {
        for (int k = 0; k < 3; k++)
            m[c * 4 + k] = rotation[c * 3 + k] * s[c];
        m[c * 4 + 3] = 0;
    }
    m[12] = t[0], m[13] = t[1], m[14] = t[2], m[15] = 1;
}
static void model_pose(Model *model) {
    if (model->posed)
        return;
    for (int k = 0; k < model->node_count; k++) {
        Node *n = &model->nodes[model->order[k]];
        float local[16];
        if (n->has_matrix)
            memcpy(local, n->matrix, sizeof local);
        else
            trs_matrix(local, n->t, n->r, n->s);
        if (n->parent >= 0)
            m4_multiply(n->world, model->nodes[n->parent].world, local);
        else
            memcpy(n->world, local, sizeof local);
    }
    model->posed = true;
}
static void model_bounds(Model *model) {
    model_pose(model);
    for (int c = 0; c < 3; c++) {
        model->low[c] = INFINITY;
        model->high[c] = -INFINITY;
    }
    for (int i = 0; i < model->node_count; i++) {
        Node *n = &model->nodes[i];
        if (n->mesh < 0)
            continue;
        ModelMesh *mesh = &model->meshes[n->mesh];
        for (int p = mesh->first; p < mesh->first + mesh->count; p++) {
            Mesh *gpu = &A.meshes[model->primitives[p].mesh];
            /* the eight corners of the primitive's box, in the model's space */
            for (int corner = 0; corner < 8; corner++) {
                float point[4] = {corner & 1 ? gpu->high[0] : gpu->low[0],
                                  corner & 2 ? gpu->high[1] : gpu->low[1],
                                  corner & 4 ? gpu->high[2] : gpu->low[2], 1},
                      world[4];
                if (n->skin >= 0)
                    memcpy(world, point, sizeof point); /* skinned meshes are in the skin's space */
                else
                    m4_transform(world, n->world, point);
                for (int c = 0; c < 3; c++) {
                    model->low[c] = fminf(model->low[c], world[c]);
                    model->high[c] = fmaxf(model->high[c], world[c]);
                }
            }
        }
    }
    if (model->low[0] > model->high[0])
        for (int c = 0; c < 3; c++)
            model->low[c] = model->high[c] = 0;
}
static int find_key(const Channel *channel, float t) {
    int low = 0, high = channel->count - 1;
    while (low < high) {
        int middle = (low + high + 1) / 2;
        if (channel->times[middle] <= t)
            low = middle;
        else
            high = middle - 1;
    }
    return low;
}
/* The channel's value at time t, into out (3 or 4 floats). */
static void sample(const Channel *channel, float t, float *out) {
    int width = channel->path == PATH_ROTATION ? 4 : 3;
    bool cubic = channel->interpolation == STEP_CUBIC;
    /* cubic splines store in-tangent, value, out-tangent for each key */
#define AP_VALUE(key) (channel->values + ((size_t)(key) * (cubic ? 3 : 1) + (cubic ? 1 : 0)) * width)
    if (channel->count == 1 || t <= channel->times[0]) {
        memcpy(out, AP_VALUE(0), width * sizeof(float));
        return;
    }
    if (t >= channel->times[channel->count - 1]) {
        memcpy(out, AP_VALUE(channel->count - 1), width * sizeof(float));
        return;
    }
    int k = find_key(channel, t);
    float t0 = channel->times[k], t1 = channel->times[k + 1], span = t1 - t0;
    float u = span > 0 ? (t - t0) / span : 0;
    const float *a = AP_VALUE(k), *b = AP_VALUE(k + 1);
    if (channel->interpolation == STEP_STEP)
        memcpy(out, a, width * sizeof(float));
    else if (cubic) {
        const float *out_tangent = a + width, *in_tangent = b - width;
        float u2 = u * u, u3 = u2 * u;
        float h00 = 2 * u3 - 3 * u2 + 1, h10 = u3 - 2 * u2 + u, h01 = -2 * u3 + 3 * u2,
              h11 = u3 - u2;
        for (int i = 0; i < width; i++)
            out[i] = h00 * a[i] + h10 * span * out_tangent[i] + h01 * b[i] + h11 * span * in_tangent[i];
    } else if (channel->path == PATH_ROTATION) {
        float dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
        float sign = dot < 0 ? -1.0f : 1.0f;
        dot *= sign;
        if (dot > 0.9995f)
            for (int i = 0; i < 4; i++)
                out[i] = a[i] + (b[i] * sign - a[i]) * u;
        else {
            float angle = acosf(dot), s = sinf(angle);
            float wa = sinf((1 - u) * angle) / s, wb = sinf(u * angle) / s * sign;
            for (int i = 0; i < 4; i++)
                out[i] = a[i] * wa + b[i] * wb;
        }
    } else
        for (int i = 0; i < 3; i++)
            out[i] = a[i] + (b[i] - a[i]) * u;
#undef AP_VALUE
    if (channel->path == PATH_ROTATION)
        quat_normalize(out);
}
