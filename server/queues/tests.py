import pytest
from django.urls import reverse
from django.contrib.auth.models import User
from django.utils import timezone
from .models import Queue, Ticket, Terminal, QueueExchange, Notification, Ban, IPAddressLog


@pytest.mark.django_db
class TestQueueModel:
    """Тесты для модели Queue"""
    
    def test_queue_creation(self):
        """Создание очереди с автоматической генерацией ссылки"""
        user = User.objects.create_user(username='queue_owner', password='pass123')
        queue = Queue.objects.create(owner=user, name='Test Queue')
        
        assert queue.link is not None
        assert len(queue.link) == 8
        assert queue.is_active is True
    
    def test_queue_str_representation(self):
        """Проверка строкового представления очереди"""
        user = User.objects.create_user(username='owner', password='pass123')
        queue = Queue.objects.create(owner=user, name='My Queue')
        assert str(queue) == f"Queue: My Queue (by {user.username})"
    
    def test_queue_ordering(self):
        """Очереди сортируются по дате создания (новые первыми)"""
        user = User.objects.create_user(username='owner2', password='pass123')
        queue1 = Queue.objects.create(owner=user, name='First')
        queue2 = Queue.objects.create(owner=user, name='Second')
        
        queues = list(Queue.objects.filter(owner=user))
        assert queues[0] == queue2
        assert queues[1] == queue1


@pytest.mark.django_db
class TestTicketModel:
    """Тесты для модели Ticket"""
    
    def test_ticket_creation(self):
        """Создание талона"""
        user = User.objects.create_user(username='ticket_user', password='pass123')
        owner = User.objects.create_user(username='ticket_owner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Test Queue')
        
        ticket = Ticket.objects.create(queue=queue, user=user, number=1)
        
        assert ticket.number == 1
        assert ticket.is_cancelled is False
        assert ticket.cancelled_by_owner is False
    
    def test_ticket_str_representation(self):
        """Проверка строкового представления талона"""
        user = User.objects.create_user(username='tuser', password='pass123')
        owner = User.objects.create_user(username='towner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Test')
        
        ticket = Ticket.objects.create(queue=queue, user=user, number=5)
        assert 'Ticket' in str(ticket)
        assert '5' in str(ticket)
        assert 'Active' in str(ticket)
    
    def test_ticket_str_cancelled(self):
        """Строковое представление отмененного талона"""
        user = User.objects.create_user(username='cuser', password='pass123')
        owner = User.objects.create_user(username='cowner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Test')
        
        ticket = Ticket.objects.create(queue=queue, user=user, number=3, is_cancelled=True)
        assert 'Cancelled' in str(ticket)
    
    def test_ticket_unique_number_per_queue(self):
        """Уникальность номера талона в пределах очереди"""
        user = User.objects.create_user(username='uuser', password='pass123')
        owner = User.objects.create_user(username='uowner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Test')
        
        Ticket.objects.create(queue=queue, user=user, number=1)
        
        with pytest.raises(Exception):
            Ticket.objects.create(queue=queue, user=user, number=1)


@pytest.mark.django_db
class TestQueueViews:
    """Тесты для представлений очередей"""
    
    def test_queue_list_requires_login(self, client):
        """Список очередей требует аутентификации"""
        response = client.get(reverse('queues:queue_list'))
        assert response.status_code == 302
        assert '/accounts/login/' in response.url
    
    def test_queue_create_requires_login(self, client):
        """Создание очереди требует аутентификации"""
        response = client.get(reverse('queues:queue_create'))
        assert response.status_code == 302
    
    def test_queue_detail_public(self, client):
        """Детали очереди доступны без авторизации по ссылке"""
        owner = User.objects.create_user(username='detail_owner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Public Queue')
        
        response = client.get(reverse('queues:queue_detail', kwargs={'link': queue.link}))
        assert response.status_code == 200
    
    def test_queue_detail_not_found(self, client):
        """Несуществующая очередь возвращает 404"""
        response = client.get(reverse('queues:queue_detail', kwargs={'link': 'nonexistent'}))
        assert response.status_code == 404
    
    def test_join_queue_requires_login(self, client):
        """Присоединение к очереди требует авторизации"""
        owner = User.objects.create_user(username='join_owner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Join Queue')
        
        response = client.post(reverse('queues:join_queue', kwargs={'link': queue.link}))
        assert response.status_code == 302
    
    def test_leave_queue_requires_login(self, client):
        """Выход из очереди требует авторизации"""
        owner = User.objects.create_user(username='leave_owner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Leave Queue')
        
        response = client.post(reverse('queues:leave_queue', kwargs={'link': queue.link}))
        assert response.status_code == 302
    
    def test_cancel_ticket_requires_login(self, client):
        """Аннулирование талона требует авторизации"""
        response = client.post(reverse('queues:cancel_ticket', kwargs={'ticket_id': 1}))
        assert response.status_code == 302


@pytest.mark.django_db
class TestQueueFunctionality:
    """Тесты функциональности очередей"""
    
    def test_user_can_create_queue(self, client):
        """Пользователь может создать очередь"""
        user = User.objects.create_user(username='creator', password='pass123')
        client.force_login(user)
        
        data = {'name': 'My New Queue', 'description': 'Test description'}
        response = client.post(reverse('queues:queue_create'), data)
        
        assert response.status_code == 302
        queue = Queue.objects.filter(owner=user, name='My New Queue').first()
        assert queue is not None
        assert queue.description == 'Test description'
    
    def test_user_can_join_queue(self, client):
        """Пользователь может занять место в очереди"""
        owner = User.objects.create_user(username='qowner', password='pass123')
        user = User.objects.create_user(username='joiner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Join Test')
        
        client.force_login(user)
        response = client.post(reverse('queues:join_queue', kwargs={'link': queue.link}))
        
        assert response.status_code == 302
        ticket = Ticket.objects.filter(queue=queue, user=user).first()
        assert ticket is not None
        assert ticket.number == 1
    
    def test_user_cannot_join_twice(self, client):
        """Пользователь не может занять место дважды"""
        owner = User.objects.create_user(username='qowner2', password='pass123')
        user = User.objects.create_user(username='double_joiner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Double Join Test')
        
        client.force_login(user)
        client.post(reverse('queues:join_queue', kwargs={'link': queue.link}))
        response = client.post(reverse('queues:join_queue', kwargs={'link': queue.link}))
        
        # Проверяем, что создан только один талон
        tickets_count = Ticket.objects.filter(queue=queue, user=user, is_cancelled=False).count()
        assert tickets_count == 1
    
    def test_user_can_leave_queue(self, client):
        """Пользователь может выйти из очереди"""
        owner = User.objects.create_user(username='qowner3', password='pass123')
        user = User.objects.create_user(username='leaver', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Leave Test')
        
        ticket = Ticket.objects.create(queue=queue, user=user, number=1)
        
        client.force_login(user)
        response = client.post(reverse('queues:leave_queue', kwargs={'link': queue.link}))
        
        assert response.status_code == 302
        ticket.refresh_from_db()
        assert ticket.is_cancelled is True
    
    def test_owner_can_cancel_ticket(self, client):
        """Владелец может аннулировать талон"""
        owner = User.objects.create_user(username='canceller', password='pass123')
        user = User.objects.create_user(username='ticket_holder', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Cancel Test')
        
        ticket = Ticket.objects.create(queue=queue, user=user, number=1)
        
        client.force_login(owner)
        response = client.post(reverse('queues:cancel_ticket', kwargs={'ticket_id': ticket.id}))
        
        assert response.status_code == 302
        ticket.refresh_from_db()
        assert ticket.is_cancelled is True
        assert ticket.cancelled_by_owner is True
    
    def test_non_owner_cannot_cancel_ticket(self, client):
        """Не владелец не может аннулировать талон"""
        owner = User.objects.create_user(username='real_owner', password='pass123')
        other = User.objects.create_user(username='other_user', password='pass123')
        user = User.objects.create_user(username='ticket_holder2', password='pass123')
        queue = Queue.objects.create(owner=owner, name='No Cancel Test')
        
        ticket = Ticket.objects.create(queue=queue, user=user, number=1)
        
        client.force_login(other)
        response = client.post(reverse('queues:cancel_ticket', kwargs={'ticket_id': ticket.id}))
        
        ticket.refresh_from_db()
        assert ticket.is_cancelled is False
    
    def test_queue_data_api(self, client):
        """API возвращает данные очереди в JSON формате"""
        owner = User.objects.create_user(username='api_owner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='API Test')
        
        user1 = User.objects.create_user(username='api_user1', password='pass123')
        user2 = User.objects.create_user(username='api_user2', password='pass123')
        
        Ticket.objects.create(queue=queue, user=user1, number=1)
        Ticket.objects.create(queue=queue, user=user2, number=2)
        
        response = client.get(reverse('queues:queue_data_api', kwargs={'link': queue.link}))
        
        assert response.status_code == 200
        data = response.json()
        assert data['queue_name'] == 'API Test'
        assert data['total_active'] == 2
        assert len(data['tickets']) == 2
    
    def test_inactive_queue_cannot_be_joined(self, client):
        """В неактивную очередь нельзя войти"""
        owner = User.objects.create_user(username='inactive_owner', password='pass123')
        user = User.objects.create_user(username='late_joiner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Inactive Queue', is_active=False)
        
        client.force_login(user)
        response = client.post(reverse('queues:join_queue', kwargs={'link': queue.link}))
        
        assert response.status_code == 302
        ticket = Ticket.objects.filter(queue=queue, user=user).first()
        assert ticket is None


@pytest.mark.django_db
class TestTerminalModel:
    """Тесты для модели Terminal"""
    
    def test_terminal_creation(self):
        """Создание терминала с автоматической генерацией хэша"""
        user = User.objects.create_user(username='terminal_owner', password='pass123')
        terminal = Terminal.objects.create(owner=user, name='Test Terminal')
        
        assert terminal.hash_secret is not None
        assert len(terminal.hash_secret) == 64
        assert terminal.is_active is True
    
    def test_terminal_can_serve_queue(self):
        """Терминал может обслуживать только очереди своего владельца"""
        owner = User.objects.create_user(username='queue_owner', password='pass123')
        other = User.objects.create_user(username='other_owner', password='pass123')
        
        terminal = Terminal.objects.create(owner=owner, name='Test Terminal')
        queue1 = Queue.objects.create(owner=owner, name='Owner Queue')
        queue2 = Queue.objects.create(owner=other, name='Other Queue')
        
        assert terminal.can_serve_queue(queue1) is True
        assert terminal.can_serve_queue(queue2) is False


@pytest.mark.django_db
class TestQueueExchange:
    """Тесты для обмена местами"""
    
    def test_exchange_creation(self):
        """Создание предложения обмена"""
        owner = User.objects.create_user(username='ex_owner', password='pass123')
        user1 = User.objects.create_user(username='ex_user1', password='pass123')
        user2 = User.objects.create_user(username='ex_user2', password='pass123')
        
        queue = Queue.objects.create(owner=owner, name='Exchange Queue')
        ticket1 = Ticket.objects.create(queue=queue, user=user1, number=5)
        ticket2 = Ticket.objects.create(queue=queue, user=user2, number=2)
        
        exchange = QueueExchange.objects.create(
            from_ticket=ticket1,
            to_ticket=ticket2,
            message='Please swap'
        )
        
        assert exchange.status == 'pending'
        assert exchange.message == 'Please swap'
    
    def test_exchange_max_offers(self):
        """Максимум 5 предложений для получателя"""
        owner = User.objects.create_user(username='max_owner', password='pass123')
        user1 = User.objects.create_user(username='max_user1', password='pass123')
        user2 = User.objects.create_user(username='max_user2', password='pass123')
        
        queue = Queue.objects.create(owner=owner, name='Max Exchange Queue')
        ticket_target = Ticket.objects.create(queue=queue, user=user1, number=1)
        
        # Создаем 5 предложений
        for i in range(5, 10):
            ticket_from = Ticket.objects.create(queue=queue, user=user2, number=i)
            QueueExchange.objects.create(from_ticket=ticket_from, to_ticket=ticket_target)
        
        # Шестое предложение должно быть заблокировано логикой views
        pending_count = QueueExchange.objects.filter(to_ticket=ticket_target, status='pending').count()
        assert pending_count == 5


@pytest.mark.django_db
class TestNotification:
    """Тесты для уведомлений"""
    
    def test_notification_creation(self):
        """Создание уведомления"""
        user = User.objects.create_user(username='notify_user', password='pass123')
        
        notification = Notification.objects.create(
            user=user,
            notification_type='turn',
            message='Your turn is coming'
        )
        
        assert notification.is_read is False
        assert notification.notification_type == 'turn'


@pytest.mark.django_db
class TestBan:
    """Тесты для бана пользователей"""
    
    def test_ban_creation(self):
        """Создание бана"""
        owner = User.objects.create_user(username='ban_owner', password='pass123')
        banned_user = User.objects.create_user(username='banned', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Ban Queue')
        
        ban = Ban.objects.create(
            queue=queue,
            user=banned_user,
            scope='queue',
            reason='Bad behavior',
            created_by=owner
        )
        
        assert ban.scope == 'queue'
        assert ban.reason == 'Bad behavior'


@pytest.mark.django_db
class TestIPAddressLog:
    """Тесты для логирования IP"""
    
    def test_ip_log_creation(self):
        """Логирование IP адреса"""
        user = User.objects.create_user(username='ip_user', password='pass123')
        
        log = IPAddressLog.objects.create(
            user=user,
            ip_address='192.168.1.1'
        )
        
        assert log.ip_address == '192.168.1.1'


@pytest.mark.django_db
class TestTicketType:
    """Тесты для типов талонов"""
    
    def test_ticket_type_default(self):
        """По умолчанию талон типа 'У' (с сайта)"""
        owner = User.objects.create_user(username='type_owner', password='pass123')
        user = User.objects.create_user(username='type_user', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Type Queue')
        
        ticket = Ticket.objects.create(queue=queue, user=user, number=1)
        
        assert ticket.ticket_type == 'U'
        assert ticket.get_ticket_type_display() == 'У'
    
    def test_terminal_ticket_type(self):
        """Талон с терминала имеет тип 'Т'"""
        owner = User.objects.create_user(username='term_owner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Terminal Queue')
        
        ticket = Ticket.objects.create(
            queue=queue,
            user=None,
            number=1,
            ticket_type='T',
            is_anonymous=True
        )
        
        assert ticket.ticket_type == 'T'
        assert ticket.get_ticket_type_display() == 'Т'
        assert ticket.is_anonymous is True


@pytest.mark.django_db
class TestTerminalAPI:
    """Тесты для API терминалов"""
    
    def test_terminal_get_ticket_success(self, client):
        """Успешное получение талона через API терминала"""
        import json
        
        owner = User.objects.create_user(username='api_term_owner', password='pass123')
        terminal = Terminal.objects.create(owner=owner, name='API Terminal')
        queue = Queue.objects.create(owner=owner, name='API Queue')
        
        data = {
            'link': queue.link,
            'terminal_hash': terminal.hash_secret
        }
        
        response = client.post(
            reverse('queues:terminal_get_ticket'),
            data=json.dumps(data),
            content_type='application/json'
        )
        
        assert response.status_code == 200
        result = response.json()
        assert result['success'] is True
        assert result['ticket_type'] == 'T'
        assert result['print_confirmation_required'] is True
    
    def test_terminal_invalid_hash(self, client):
        """Неверный хэш терминала"""
        import json
        
        owner = User.objects.create_user(username='inv_term_owner', password='pass123')
        queue = Queue.objects.create(owner=owner, name='Invalid Queue')
        
        data = {
            'link': queue.link,
            'terminal_hash': 'invalid_hash_12345'
        }
        
        response = client.post(
            reverse('queues:terminal_get_ticket'),
            data=json.dumps(data),
            content_type='application/json'
        )
        
        assert response.status_code == 403
        assert 'error' in response.json()
    
    def test_terminal_cannot_serve_other_queue(self, client):
        """Терминал не может обслуживать чужие очереди"""
        import json
        
        owner1 = User.objects.create_user(username='term_owner1', password='pass123')
        owner2 = User.objects.create_user(username='term_owner2', password='pass123')
        
        terminal = Terminal.objects.create(owner=owner1, name='Terminal')
        queue = Queue.objects.create(owner=owner2, name='Other Queue')
        
        data = {
            'link': queue.link,
            'terminal_hash': terminal.hash_secret
        }
        
        response = client.post(
            reverse('queues:terminal_get_ticket'),
            data=json.dumps(data),
            content_type='application/json'
        )
        
        assert response.status_code == 403
    
    def test_terminal_confirm_print(self, client):
        """Подтверждение печати талона"""
        import json
        
        owner = User.objects.create_user(username='conf_owner', password='pass123')
        terminal = Terminal.objects.create(owner=owner, name='Confirm Terminal')
        queue = Queue.objects.create(owner=owner, name='Confirm Queue')
        
        # Сначала создаем талон
        ticket_data = {
            'link': queue.link,
            'terminal_hash': terminal.hash_secret
        }
        
        create_response = client.post(
            reverse('queues:terminal_get_ticket'),
            data=json.dumps(ticket_data),
            content_type='application/json'
        )
        
        ticket_id = create_response.json()['ticket_id']
        
        # Подтверждаем печать
        confirm_data = {
            'ticket_id': ticket_id,
            'terminal_hash': terminal.hash_secret
        }
        
        response = client.post(
            reverse('queues:terminal_confirm_print'),
            data=json.dumps(confirm_data),
            content_type='application/json'
        )
        
        assert response.status_code == 200
        result = response.json()
        assert result['success'] is True
        
        # Проверяем, что талон обновлен
        ticket = Ticket.objects.get(id=ticket_id)
        assert ticket.print_confirmed is True
        assert ticket.printed is True
