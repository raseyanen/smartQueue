# SmartQueue Server

Django-сервер системы управления очередями SmartQueue.

## Возможности

- Создание очередей с уникальными короткими ссылками
- Защита очередей паролем
- Выдача талонов через веб-интерфейс и терминалы ESP32
- Живое обновление очереди с подсчётом времени ожидания
- Обмен местами между участниками (до 5 предложений на пользователя)
- Уведомления о приближении очереди
- Бан пользователей в конкретной очереди или во всех очередях владельца
- Логирование IP адресов для предотвращения мультиаккаунтов
- Архив завершённых талонов
- Экспорт списков участников в CSV
- Проверка талонов по хешу

## API Endpoints

### Терминалы

#### POST /queues/api/terminal/ticket/
Получение анонимного талона с терминала.

**Request:**
```json
{
    "link": "abc12345",
    "terminal_hash": "a1b2c3d4e5f6..."
}
```

**Response:**
```json
{
    "success": true,
    "ticket_number": 42,
    "ticket_id": 123,
    "ticket_type": "T",
    "queue_name": "My Queue",
    "print_confirmation_required": true
}
```

#### POST /queues/api/terminal/confirm/
Подтверждение печати талона.

**Request:**
```json
{
    "ticket_id": 123,
    "terminal_hash": "a1b2c3d4e5f6..."
}
```

**Response:**
```json
{
    "success": true,
    "message": "Print confirmed. Ticket is now active in queue.",
    "ticket_number": 42,
    "position": 5
}
```

### Проверка талона

#### GET /queues/verify/<ticket_hash>/
Проверка действительности талона по хешу.

### Данные очереди

#### GET /queues/<link>/api/
Получение живых данных очереди.

**Response:**
```json
{
    "queue_name": "My Queue",
    "is_active": true,
    "total_active": 10,
    "queue_age_minutes": 45,
    "avg_service_time": 2,
    "tickets": [
        {
            "id": 1,
            "number": 1,
            "ticket_type": "У",
            "username": "user1",
            "created_at": "22.05.2026 19:00",
            "position": 1,
            "wait_time_minutes": 0,
            "notified": false
        }
    ],
    "current_position": 3
}
```

## Модели данных

### Queue
- `owner` - владелец очереди (User)
- `name` - название
- `description` - описание
- `link` - короткая ссылка (8 символов UUID)
- `password` - опциональный пароль
- `is_active` - активна ли очередь
- `created_at` - дата создания

### Ticket
- `queue` - очередь (ForeignKey)
- `user` - пользователь (nullable для анонимных)
- `number` - номер в очереди
- `ticket_type` - тип: 'У' (сайт) или 'Т' (терминал)
- `is_cancelled` - отменён ли
- `is_anonymous` - анонимный ли
- `printed` - напечатан ли
- `print_confirmed` - подтверждена ли печать
- `ip_address` - IP адрес
- `ticket_hash` - хеш для проверки
- `notified` - отправлено ли уведомление

### Terminal
- `owner` - владелец (User)
- `hash_secret` - секретный хеш для API
- `name` - название терминала
- `mac_address` - MAC адрес устройства
- `last_seen` - последнее подключение
- `firmware_version` - версия прошивки
- `is_active` - активен ли

### CompletedTicket
Архив завершённых талонов (аннулированных или вышедших).

## Запуск через Docker

```bash
docker-compose up --build
```

Сервер будет доступен на http://localhost:8000

## Ручной запуск

```bash
cd server
pip install -r requirements.txt
python manage.py migrate
python manage.py runserver
```

## Личный кабинет пользователя

- `/queues/` - список очередей
- `/queues/create/` - создание очереди
- `/queues/terminals/` - управление терминалами
- `/queues/notifications/` - уведомления
- `/queues/<link>/participants/` - список активных участников
- `/queues/<link>/finished/` - список завершивших очередь
- `/queues/<link>/export/?type=active|finished` - экспорт CSV

## Хеш талона

Хеш генерируется на основе:
```
{queue.id}:{number}:{ticket_type}:{created_at.timestamp()}
```

Используется SHA-256 для создания уникального идентификатора.

## QR-коды

Для верификации талона можно использовать QR-код, содержащий URL:
```
http://<site.url>/queues/verify/<ticket_hash>/
```
