# xscJson
- xushun
- 2022/7/10

这是一个用C语言编写的简易JSON库

## 特性

- 使用标准C语言编写
- 跨平台/编译器
- 完成了合乎标准的JSON解析器和生成器
- 使用递归下降解析器
- 使用双精度`double`类型存储JSON NUMBER类型
- 支持UTF-8编码的JSON文本


## 接口说明

### 头文件
`xscjson.h`，其中包含了全部的API、JSON值和JSON对象成员的结构体、JSON类型和错误类型的枚举类型。


### 数据结构和类型

#### JSON数据类型枚举
```c
typedef enum { 
    JSON_NULL, 
    JSON_FALSE, 
    JSON_TRUE, 
    JSON_NUMBER, 
    JSON_STRING, 
    JSON_ARRAY, 
    JSON_OBJECT
} json_type;
```

#### 错误类型枚举
```c
enum {
    JSON_PARSE_OK = 0,                      // 成功解析
    JSON_PARSE_EXPECT_VALUE,                // 只有空白字符
    JSON_PARSE_INVALID_VALUE,               // 非法json值(非法的字面量)
    JSON_PARSE_ROOT_NOT_SINGULAR,           // value whitespace 后还有其他 value
    JSON_PARSE_NUMBER_TOO_BIG,              // 数字超过double的范围
    JSON_PARSE_MISS_QUOTATION_MARK,         // 字符串的第二个双引号丢失
    JSON_PARSE_INVALID_STRING_ESCAPE,       // 非法的转义字符
    JSON_PARSE_INVALID_STRING_CHAR,         // 非法的字符串字符
    JSON_PARSE_INVALID_UNICODE_HEX,         // 非法的Unicode \u之后不是4位16进制数
    JSON_PARSE_INVALID_UNICODE_SURROGATE,   // 只有高代理项而欠缺低代理项 或低代理项不在合法码点范围中
    JSON_PARSE_MISS_COMMA_OR_SQUARE_BRACKET,// 逗号或方括号丢失
    JSON_PARSE_MISS_KEY,                    // json对象成员的key丢失
    JSON_PARSE_MISS_COLON,                  // 冒号丢失
    JSON_PARSE_MISS_COMMA_OR_CURLY_BRACKET  // 逗号或大括号丢失
};
```

#### JSON值数据结构
JSON值使用`json_value`结构体实现，`json_type`标志JSON值的类型，为实现不同的JSON类型值以及尽量节省内存，在`json_value`内部使用了`union`：  
- `o`，`JSON_OBJECT`，使用动态数组实现，`m`为数组头指针，`size`为当前对象成员数量，`capacity`为动态数组的大小；
- `a`，`JSON_ARRAY`，使用动态数组实现，`e`为数组头指针，`size`为当前JSON值数量，`capacity`为动态数组的大小；
- `s`，`JSON_STRING`，`s`为字符串指针，`len`为字符串长度；
- `n`，`JSON_NUMBER`。

JSON对象成员使用`json_member`结构体实现：  
- `k`，JSON对象成员键字符串指针；
- `klen`，键字符串长度；
- `v`，JSON对象成员值。

```c
typedef struct json_value json_value;
typedef struct json_member json_member;

struct json_value {
    union {
        struct { json_member* m; size_t size, capacity; } o;
        struct { json_value* e; size_t size, capacity; } a;
        struct { char* s; size_t len; } s;
        double n;
    } u;
    json_type type;
};

struct json_member {
    char* k;
    size_t klen;
    json_value v;
};
```



### API

#### json_value基础操作

- `#define json_init(v) do { (v)->type = JSON_NULL; } while(0)`
  - 初始化`v`将其类型设置为`JSON_NULL`
- `void json_free(json_value* v);`
  - 释放`v`中分配的内存，并将其类型设置为`JSON_NULL`
- `int json_parse(json_value* v, const char* json);`
  - JSON解析器，将`json`中的JSON字符串，解析并存储到`v`中
  - 返回值为`JSON_PARSE_OK`，或其他错误类型
  - 解析器解析JSON字符串时，会使用一个动态堆栈来保存临时数据，全部解析完成后，从堆栈中弹出数据并存储到`v`中
- `char* json_stringify(const json_value* v, size_t* length);`
  - JSON生成器，从`v`中的数据生成一个正确的JSON字符串
  - 返回字符串的首地址，并设置长度`length`(该变量由使用者管理其内存)
  - 生成器生成JSON字符串时，也会使用动态堆栈来临时存储数据，字符串生成完成后返回堆栈的首地址，使用者需要在使用后释放内存
- `void json_copy(json_value* dst, const json_value* src);`
  - 将`src`的数据完全拷贝给`dst`(深度拷贝，`src`保持不变)
- `void json_move(json_value* dst, json_value* src);`
  - 将`src`持有的数据转交给`dst`(会释放`dst`原来持有的内存)
- `void json_swap(json_value* lhs, json_value* rhs);`
  - 交换两个JSON值

#### json_value访问操作

- `json_type json_get_type(const json_value* v);`
  - 获得`v`的JSON类型
- `int json_is_equal(const json_value* lhs, const json_value* rhs);`
  - 比较两个json值是否相同
  - 相同返回`1`，不同返回`0`
  - 比较`JSON_ARRAY`和`JSON_OBJECT`时会递归调用
  - 比较`JSON_OBJECT`时，忽略其对象成员的顺序
- `#define json_set_null(v) json_free(v)`
  - 将`v`释放并设置为`JSON_NULL`

#### 布尔类型访问操作

- `int json_get_boolean(const json_value* v);`
  - 判断`v`是否为`JSON_TRUE`
  - `v`的类型为`JSON_TRUE`返回`1`，为`JSON_FALSE`返回`0`
  - `v`必须为这两种类型之一
- `void json_set_boolean(json_value* v, int b);`
  - 将`v`(可以为任意json类型)设置为布尔类型
  - `b`为`0`设置为`JSON_FALSE`，否则`JSON_TRUE`

#### JSON_NUMBER访问操作

- `double json_get_number(const json_value* v);`
  - 获得`v`(必须为`JSON_NUMBER`)的数值
- `void json_set_number(json_value* v, double n);`
  - 将`v`(任意json类型)设置为`JSON_NUMBER`，并设置其数值`n`


#### JSON_STRING访问操作

- `const char* json_get_string(const json_value* v);`
  - 获得`v`(`JSON_STRING`类型)的字符串头指针
- `size_t json_get_string_length(const json_value* v);`
  - 获得`v`(`JSON_STRING`类型)的字符串长度
- `void json_set_string(json_value* v, const char* s, size_t len);`
  - 将`v`(任意类型)设置为`JSON_STRING`，并设置其字符串(重新分配内存)和字符串长度

#### JSON_ARRAY访问操作

- `void json_set_array(json_value* v, size_t capacity);`
  - 释放`v`的内存，并重新分配`capacity * sizeof(json_value)`大小的内存，用于初始化`JSON_ARRAY`的情况
- `size_t json_get_array_size(const json_value* v);`
  - 获得`v`数组中现有的json值的数量
- `size_t json_get_array_capacity(const json_value* v);`
  - 获得`v`数组中动态数组的大小
- `void json_reserve_array(json_value* v, size_t capacity);`
  - 当`capacity`比当前`v`数组中动态数组容量大时，为`v`重新分配动态数组空间(拷贝数据)
- `void json_shrink_array(json_value* v);`
  - 将`v`数组的动态数组大小削减到，`v`中json值的数量，减少内存的使用
- `void json_clear_array(json_value* v);`
  - 清空`v`中所有的json值(调用`json_erase_array_element()`实现)
- `json_value* json_get_array_element(const json_value* v, size_t index);`
  - 获得`v`中下标为`index`的json值
- `json_value* json_pushback_array_element(json_value* v);`
  - 向`v`数组末尾添加一个json值，数组空间不足会动态扩展
- `void json_popback_array_element(json_value* v);`
  - 释放`v`数组中最后一个json值
- `json_value* json_insert_array_element(json_value* v, size_t index);`
  - 在`index`位置插入一个json值
  - 返回值为插入位置的指针
- `void json_erase_array_element(json_value* v, size_t index, size_t count);`
  - 从`v`数组`index`位置开始，删除`count`个json值
  - 从`index + 1`位置向前覆盖


#### JSON_OBJECT访问操作

- `void json_set_object(json_value* v, size_t capacity);`
  - 释放并重新分配`v`的内存，大小为`capacity * sizeof(json_member)`
- `size_t json_get_object_size(const json_value* v);`
  - 获得`v`中现有的对象成员数量
- `size_t json_get_object_capacity(const json_value* v);`
  - 获得`v`数组容量大小
- `void json_reserve_object(json_value* v, size_t capacity);`
  - 如果`capacity`大于当前动态数组容量，重新为其分配内存，拷贝
- `void json_shrink_object(json_value* v);`
  - 将`v`数组容量缩小到其现有member数量
- `void json_clear_object(json_value* v);`
  - 清空`v`中所有member
- `const char* json_get_object_key(const json_value* v, size_t index);`
  - 获得`index`位置的member的键
- `size_t json_get_object_key_length(const json_value* v, size_t index);`
  - 获得`index`位置的member的键的长度
- `json_value* json_get_object_value(const json_value* v, size_t index);`
  - 获得`index`位置的member的值
- `#define JSON_KEY_NOT_EXIST ((size_t)-1)`
  - 宏定义，代表`JSON_OBJECT`中不存在这样的键
- `size_t json_find_object_index(const json_value* v, const char* key, size_t klen);`
  - 按键查找某个member，返回其index
  - 若不存在，返回上一项的宏
- `json_value* json_find_object_value(json_value* v, const char* key, size_t klen);`
  - 按键查找某个member，返回其值的指针
  - 若不存在，返回`NULL`
- `json_value* json_set_object_value(json_value* v, const char* key, size_t klen);`
  - 如果`v`中存在某个member的键和`key`相同，返回该member的值指针
  - 如果`v`中不存在这样的member，向动态数组中添加一个member，并将其键设置为`key klen`，并返回该member的值的指针
- `void json_remove_object_value(json_value* v, size_t index);`
  - 在`v`中删掉`index`位置的member，从`index + 1`位置向前覆盖

### 编译和测试
```shell
cd build
make

.\test
```
`test.c`文件中提供了全部接口的测试用例，可以自行添加测试用例。

-----
该json库参考miloyip大佬的json-tutorial教程实现。