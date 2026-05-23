# SmartQueue - Система управления очередями

## Описание

SmartQueue - это полноценная система управления очередями с поддержкой веб-интерфейса, терминалов на ESP32, матричных принтеров и мобильного доступа.

## Возможности

### Для пользователей:
- Создание очередей с уникальными короткими ссылками
- Защита очередей паролем
- Занятие мест в очереди и получение талонов
- Живое обновление статуса очереди
- Подсчёт приблизительного времени ожидания
- Уведомления когда подходит очередь
- Обмен местами с другими участниками (до 5 попыток)
- Просмотр истории посещений

### Для владельцев очередей:
- Аннулирование любых талонов
- Бан пользователей (в конкретной очереди или во всех очередях)
- Просмотр списков активных и завершивших очередь участников
- Экспорт данных в CSV
- Управление терминалами

### Для терминалов (ESP32):
- Выдача анонимных талонов с меткой "Т"
- Поддержка матричных принтеров FunnyPrint/Epson
- OLED дисплей 128x64 (опционально)
- WiFi (WPA2-Personal и WPA2-Enterprise)
- Режим точки доступа для настройки
- Веб-интерфейс конфигурации с паролем
- OTA обновления
- Автоматическая генерация хеша на основе MAC адреса

## Структура проекта

```
smartQueue/
├── server/                 # Django сервер
│   ├── accounts/          # Приложение аутентификации
│   ├── queues/            # Приложение очередей
│   ├── config/            # Настройки Django
│   ├── manage.py
│   ├── requirements.txt
│   └── Dockerfile
├── terminal/              # Прошивка для ESP32
│   ├── src/
│   │   └── main.cpp
│   ├── include/
│   ├── data/
│   └── platformio.ini
├── docker-compose.yml
└── README.md
```

## Быстрый старт с Docker

### Требования
- Docker и Docker Compose
- Python 3.11+ (для локальной разработки)

### Запуск сервера

```bash
# Клонирование репозитория
git clone <repository-url>
cd smartQueue

# Запуск через Docker Compose
docker-compose up -d

# Сервер доступен по адресу http://localhost:8000
```

### Локальная разработка

```bash
cd server

# Создание виртуального окружения
python -m venv .venv
source .venv/bin/activate  # Linux/Mac
# или .venv\Scripts\activate  # Windows

# Установка зависимостей
pip install -r requirements.txt

# Миграции базы данных
python manage.py migrate

# Создание суперпользователя
python manage.py createsuperuser

# Запуск сервера
python manage.py runserver
```

## API Документация

### Терминальное API

#### Получение талона
```http
POST /queues/api/terminal/ticket/
Content-Type: application/json
X-Terminal-Hash: <hash_терминала>

{
    "link": "<ссылка_очереди>",
    "ip_address": "192.168.1.100"
}
```

Ответ:
```json
{
    "success": true,
    "ticket_number": 42,
    "ticket_type": "Т",
    "ticket_hash": "abc123...",
    "message": "Талон успешно создан"
}
```

#### Подтверждение печати
```http
POST /queues/api/terminal/confirm/
Content-Type: application/json
X-Terminal-Hash: <hash_терминала>

{
    "ticket_number": 42,
    "confirmed": true
}
```

### Проверка талона по хешу
```http
GET /queues/api/ticket/verify/<ticket_hash>/
```

## Модели данных

### Queue (Очередь)
- `owner` - владелец очереди
- `name` - название
- `description` - описание
- `link` - короткая ссылка для доступа
- `password` - пароль на очередь (опционально)
- `is_active` - активна ли очередь

### Ticket (Талон)
- `queue` - очередь
- `user` - пользователь (может быть null для анонимных)
- `number` - порядковый номер
- `ticket_type` - тип ('У' - сайт, 'Т' - терминал)
- `is_cancelled` - отменён ли
- `ticket_hash` - хеш для проверки
- `ip_address` - IP адрес получения

### Terminal (Терминал)
- `owner` - владелец
- `hash_secret` - секретный хеш для API
- `mac_address` - MAC адрес устройства
- `firmware_version` - версия прошивки

### QueueExchange (Обмен местами)
- `from_ticket` - кто предлагает
- `to_ticket` - кому предлагают
- `message` - сообщение
- `status` - статус (pending/accepted/declined/expired)
- `offer_count` - счётчик попыток

### Ban (Бан)
- `queue` - очередь
- `user` - забаненный пользователь
- `scope` - область (queue/owner)
- `reason` - причина

## Конфигурация терминала ESP32

### Подключение пинов
```
ESP32-C3 SuperMicro:
- BUTTON_PIN: GPIO4 (кнопка печати)
- LED_PIN: GPIO5 (индикация)
- PRINTER_RX: GPIO6 (приём от принтера)
- PRINTER_TX: GPIO7 (передача принтеру)
- OLED_SDA: GPIO8 (опционально)
- OLED_SCL: GPIO9 (опционально)
```

### Первоначальная настройка
1. При первом включении терминал создаёт точку доступа "SmartQueue_Terminal"
2. Пароль по умолчанию: "12345678"
3. Подключитесь к сети и откройте http://192.168.4.1
4. Введите логин: admin, пароль: admin
5. Настройте:
   - URL сервера (например, http://192.168.1.100:8000)
   - Хеш терминала (получить в личном кабинете на сервере)
   - WiFi сеть и пароль
   - Пароль веб-интерфейса

### Компиляция прошивки
```bash
cd terminal

# Установка PlatformIO
pip install platformio

# Сборка
pio run

# Загрузка на устройство
pio run --target upload

# Мониторинг последовательного порта
pio device monitor
```

## Личный кабинет пользователя

### Функции:
- Просмотр своих активных талонов
- История посещений очередей
- Управление терминалами:
  - Создание новых терминалов
  - Просмотр хешей и статистики
  - Деактивация терминалов
- Экспорт данных очередей в CSV

## Безопасность

### Защита от мультиаккаунтов
- Логирование IP адресов
- Возможность бана по IP
- Ограничение на создание талонов с одного IP

### Аутентификация терминалов
- Уникальный хеш для каждого терминала
- Проверка владельца терминала и очереди
- HTTPS рекомендуется для production

## Переменные окружения

```env
SECRET_KEY=ваш_секретный_ключ
DEBUG=True
ALLOWED_HOSTS=localhost,127.0.0.1,yourdomain.com
DATABASE_URL=postgresql://user:pass@host:5432/smartqueue
POSTGRES_DB=smartqueue
POSTGRES_USER=smartqueue
POSTGRES_PASSWORD=secure_password
POSTGRES_HOST=db
POSTGRES_PORT=5432
CORS_ALLOWED_ORIGINS=http://localhost:8000
```

## Тестирование

```bash
cd server

# Запуск тестов
pytest

# Запуск с покрытием
pytest --cov=accounts --cov=queues
```

## Производство

### Рекомендации:
1. Используйте PostgreSQL вместо SQLite
2. Настройте HTTPS
3. Измените SECRET_KEY
4. Отключите DEBUG режим
5. Используйте gunicorn вместо встроенного сервера
6. Настройте резервное копирование БД

### Развёртывание с gunicorn:
```bash
gunicorn config.wsgi:application --bind 0.0.0.0:8000 --workers 4
```

## Лицензия

MIT License

## Контакты

Для вопросов и предложений создавайте Issues в репозитории.
