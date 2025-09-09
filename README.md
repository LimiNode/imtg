# imtg
Immediate-mode Telegram Bot UI library for C++17. Render text and inline keyboards each update, framework handles diffs and API calls.

В СТАДИИ РАЗРАБОТКИ

План:

«Immediate-mode UI» для Telegram-ботов = каждый апдейт заново «рисуете» интерфейс (текст + inline-клавиатура), а движок сам вычисляет дифф и отправляет минимальный набор вызовов Telegram Bot API (`editMessageText`, `editMessageReplyMarkup`, `sendMessage`).

## Ключевая мысль

* Рендерим «дерево» за тик: `Begin(chat) → Text()/Button()/InputText() → End()`.
* Движок хранит на пользователя (chat\_id) снэпшот прошлого «дерева» и состояние виджетов (по ID).
* На вход приходят `update` (message/callback\_query). Диспетчер кладёт событие в контекст и зовёт ваш `Draw()`.
* После `End()` рантайм делает diff и вызывает подходящее API: чаще всего один `editMessageText` с новой разметкой и/или `editMessageReplyMarkup`. Если сообщения ещё нет — `sendMessage`.

## Архитектура (минимум)

* `TgRuntime`: цикл, polling/webhook, rate-limit, очередь запросов.
* `SessionStore`: по `chat_id` → `ImTgContext` (состояние виджетов, last snapshot).
* `ImTg` API (header-only): immediate-вызовы добавляют ноды в «виртуальное сообщение».
* `Reconcilier`: сравнивает предыдущий снэпшот и текущий, решает `editMessage* / sendMessage`.
* `Adapter`: HTTP + JSON (можно подключить ваш kurlyk для HTTP и rapidjson/nlohmann-json).

Инварианты:

* 1 активное «корневое» сообщение на чат (экономит шум). История — опционально.
* Все виджеты имеют стабильные ID (как в ImGui): `PushID/PopID`, `id` в сигнатуре.
* Вся логика — чистая функция от `State + Update` к «виртуальному сообщению».

## Минимальный интерфейс (эскиз)

```cpp
/// C++17, header-only стиль, без исключений (по желанию)
namespace imtg {

using ChatID = long long;

struct InlineButton {
    std::string id;      // стабильный ID события
    std::string label;   // подпись
    int         w = 1;   // ширина в «ячейках» (для разметки рядов)
};

struct MessageDraft {
    std::string text_md;                     // MarkdownV2 или HTML
    std::vector<std::vector<InlineButton>> kb;// клавиатура построчно
};

struct Update {
    ChatID chat_id{};
    std::string type;         // "message", "callback"
    std::string data;         // текст или callback_data
};

struct IO {
    // Абстракция транспорта. Реализация дергает Telegram API.
    bool send_message(ChatID, const MessageDraft&, int* out_msg_id);
    bool edit_text(ChatID, int msg_id, const MessageDraft&);
    bool edit_markup(ChatID, int msg_id, const MessageDraft&);
};

struct Context {
    ChatID chat{};
    int    root_msg_id = 0;        // «хост» интерфейса
    // Хранилище состояний виджетов: map<ImGuiID, Variant>
    // + last_snapshot для diff
};

void Begin(Context& ctx, ChatID chat);
void End(Context& ctx);

// Виджеты (возвращают «событие» по текущему апдейту и рисуют в Draft)
void Text(const char* fmt, ...);                   // копится в текущем Draft
bool Button(const char* id, const char* label);    // true, если на этом тике пришёл клик
bool Checkbox(const char* id, bool* v);
bool InputText(const char* id, std::string& buf, int max_len = 256);

// Утилиты идентификации
void PushID(const char* str_id);
void PopID();

} // namespace imtg
```

## Алгоритм reconcile (опорные шаги)

1. Если `root_msg_id==0` → `sendMessage` с черновиком → сохранить `root_msg_id`.
2. Иначе сравнить `old.text_md` vs `new.text_md` и `old.kb` vs `new.kb`:

   * менялся текст → `editMessageText` (сразу с новой клавиатурой).
   * менялась только клавиатура → `editMessageReplyMarkup`.
3. Дедупликация (debounce) и троттлинг под лимиты Telegram (например, ≥50 msg/min на чат).

## MWE: счётчик + чекбокс + инпут

Сборка (пример):
`g++ -std=gnu++17 -O2 bot.cpp -o bot`

```cpp
#include <string>
#include <unordered_map>
#include <cstdio>
#include <cstdarg>

// --- Имитация маленького immediate-ядра (упрощено до MWE) ---
namespace imtg {
using ChatID = long long;

struct InlineButton { std::string id, label; };
struct MessageDraft {
    std::string text_md;
    std::vector<std::vector<InlineButton>> kb;
};

struct Update {
    ChatID chat_id{};
    std::string type; // "message"|"callback"
    std::string data; // text or callback_data
};

struct Context {
    ChatID chat{};
    int root_msg_id = 0;
    MessageDraft draft, last;
    std::vector<std::string> id_stack;
    std::unordered_map<std::string, std::string> store; // key->serialized
    const Update* cur_update = nullptr;
};

static thread_local Context* g_ctx = nullptr;

static std::string make_id(const std::string& local) {
    std::string full;
    for (auto& p: g_ctx->id_stack) { full += p; full += "/"; }
    full += local;
    return full;
}

void PushID(const char* s){ g_ctx->id_stack.emplace_back(s?s:""); }
void PopID(){ if(!g_ctx->id_stack.empty()) g_ctx->id_stack.pop_back(); }

static void vappend(std::string& dst, const char* fmt, va_list ap) {
    char buf[2048];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    dst += buf;
}

void Text(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vappend(g_ctx->draft.text_md, fmt, ap);
    va_end(ap);
    g_ctx->draft.text_md += "\n";
}

bool Button(const char* id, const char* label) {
    std::string fid = make_id(id);
    // рисуем кнопку
    if (g_ctx->draft.kb.empty() || g_ctx->draft.kb.back().size()>=2)
        g_ctx->draft.kb.emplace_back();
    g_ctx->draft.kb.back().push_back({fid, label});
    // событие?
    if (g_ctx->cur_update && g_ctx->cur_update->type=="callback")
        return g_ctx->cur_update->data == fid;
    return false;
}

bool Checkbox(const char* id, bool* v) {
    std::string fid = make_id(id);
    std::string key = "chk:" + fid;
    // загрузка
    auto it = g_ctx->store.find(key);
    if (it != g_ctx->store.end()) *v = (it->second=="1");
    // рендер
    const char* box = *v ? "☑" : "☐";
    bool toggled = Button((fid + "/toggle").c_str(), (std::string(box) + " " + id).c_str());
    if (toggled) { *v = !*v; g_ctx->store[key] = *v ? "1":"0"; }
    return toggled;
}

bool InputText(const char* id, std::string& buf, int max_len=256) {
    // В Telegram нет инлайнового поля ввода → паттерн: кнопка «✏» -> бот ждёт следующее текстовое сообщение как ввод.
    // Для MWE: если прилетело message и мы «ждём» этого id, берём текст.
    std::string fid = make_id(id);
    std::string wait_key = "wait:" + fid;
    bool waiting = (g_ctx->store[wait_key]=="1");

    // Рисуем: текст + кнопка ✏
    Text("*%s*: `%s`", id, buf.c_str());
    bool ask = Button((fid + "/ask").c_str(), "✏");

    bool changed = false;
    if (ask) { g_ctx->store[wait_key]="1"; }
    if (g_ctx->cur_update && g_ctx->cur_update->type=="message" && waiting) {
        std::string s = g_ctx->cur_update->data;
        if ((int)s.size() > max_len) s.resize((size_t)max_len);
        if (s != buf) { buf = s; changed = true; }
        g_ctx->store[wait_key]="0";
    }
    return changed;
}

void Begin(Context& ctx, ChatID chat) { g_ctx = &ctx; g_ctx->chat = chat; g_ctx->draft = {}; }
void End(Context& ctx) { g_ctx = nullptr; }

// --- Заглушка транспорта ---
struct IO {
    bool send_message(ChatID, const MessageDraft&, int* out){ *out = 42; return true; }
    bool edit_text(ChatID, int, const MessageDraft&){ return true; }
    bool edit_markup(ChatID, int, const MessageDraft&){ return true; }
};

// Простейший reconcile
void Reconcile(IO& io, Context& c) {
    if (c.root_msg_id==0) { int id=0; io.send_message(c.chat, c.draft, &id); c.root_msg_id=id; c.last=c.draft; return; }
    bool text_changed = (c.draft.text_md != c.last.text_md);
    bool kb_changed   = (c.draft.kb.size() != c.last.kb.size()); // упрощено
    if (text_changed) { io.edit_text(c.chat, c.root_msg_id, c.draft); c.last=c.draft; }
    else if (kb_changed) { io.edit_markup(c.chat, c.root_msg_id, c.draft); c.last=c.draft; }
}

} // namespace imtg

// --- Пример приложения: счётчик/чекбокс/инпут ---
struct App {
    std::unordered_map<imtg::ChatID, imtg::Context> sessions;
    imtg::IO io;

    void on_update(const imtg::Update& up) {
        auto& ctx = sessions[up.chat_id];
        ctx.cur_update = &up;

        imtg::Begin(ctx, up.chat_id);
        imtg::PushID("root");

        static std::unordered_map<imtg::ChatID,int> counter;
        static std::unordered_map<imtg::ChatID,bool> flag;
        static std::unordered_map<imtg::ChatID,std::string> name;
        auto& cnt  = counter[up.chat_id];
        auto& flg  = flag[up.chat_id];
        auto& nm   = name[up.chat_id];

        imtg::Text("*Demo Bot*");
        if (imtg::Button("inc", "➕")) cnt++;
        if (imtg::Button("dec", "➖")) cnt--;
        imtg::Text("Count = `%d`", cnt);

        imtg::Checkbox("enable-advanced", &flg);
        if (flg) {
            if (nm.empty()) nm = "guest";
            imtg::InputText("name", nm, 32);
            imtg::Text("Hello, _%s_!", nm.c_str());
        }

        imtg::PopID();
        imtg::End(ctx);

        imtg::Reconcile(io, ctx);
        ctx.cur_update = nullptr;
    }
};

int main() {
    App app;

    // Симуляция: новое сообщение "start"
    app.on_update({12345, "message", "/start"});
    // Клик по кнопке inc
    app.on_update({12345, "callback", "root/inc"});
    // Включить чекбокс
    app.on_update({12345, "callback", "root/enable-advanced/toggle"});
    // Нажать «✏», затем прислать текст "Alice"
    app.on_update({12345, "callback", "root/name/ask"});
    app.on_update({12345, "message", "Alice"});

    return 0;
}
```

## Что учесть

* **Разметка**: поддержка MarkdownV2/HTML, экранирование спецсимволов.
* **Rate-limit/дребезг**: батч/дебаунс `edit*`, экспоненциальный бэкофф по 429.
* **Webhooks**: вместо polling, отдельный адаптер.
* **Многооконность**: «страницы»/«экраны» как стэк (Push/PopPage), каждый со своей `Draw()`.
* **Навигация и формы**: `BeginForm/EndForm`, встроенная валидация, подсветка ошибок.
* **Таблицы/пагинация**: генератор таблицы в текст + кнопки «Prev/Next».
* **Сериализация состояния**: на диске/БД (по `chat_id`, TTL).
* **Безопасность**: CSRF в callback\_data (nonce/session), ограничения на длину.
* **Логи и трассировка**: события, метрики (`editMessageText`/s).

## Как начать интеграцию

1. Подменить `IO` на реализацию через ваш HTTP-клиент.
2. Добавить JSON-сериализацию (nlohmann::json/rapidjson) для `/getUpdates` и методов отправки.
3. Обернуть цикл: `poll → parse Update → app.on_update(update)`.
