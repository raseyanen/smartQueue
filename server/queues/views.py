from django.shortcuts import render, redirect, get_object_or_404
from django.contrib.auth.decorators import login_required
from django.contrib import messages
from django.http import JsonResponse
from django.utils import timezone
from django.db.models import Count, Q
from django.views.decorators.csrf import csrf_exempt
from django.core.exceptions import PermissionDenied
import json
from .models import Queue, Ticket, Terminal, QueueExchange, Notification, Ban, IPAddressLog
from .views.dashboard_views import dashboard


def get_client_ip(request):
    """Получение IP адреса клиента"""
    x_forwarded_for = request.META.get('HTTP_X_FORWARDED_FOR')
    if x_forwarded_for:
        ip = x_forwarded_for.split(',')[0]
    else:
        ip = request.META.get('REMOTE_ADDR')
    return ip


def log_user_ip(request):
    """Логирование IP адреса пользователя"""
    if request.user.is_authenticated:
        ip = get_client_ip(request)
        IPAddressLog.objects.get_or_create(user=request.user, ip_address=ip)


def check_user_banned(user, queue):
    """Проверка, забанен ли пользователь"""
    # Проверка бана в конкретной очереди
    if Ban.objects.filter(queue=queue, user=user, scope='queue').exists():
        return True
    # Проверка бана во всех очередях владельца
    if Ban.objects.filter(queue__owner=queue.owner, user=user, scope='owner').exists():
        return True
    return False


@login_required
def queue_list(request):
    """Список очередей пользователя"""
    log_user_ip(request)
    owned_queues = Queue.objects.filter(owner=request.user).annotate(
        active_participants_count=Count('tickets', filter=Q(tickets__is_cancelled=False))
    )
    return render(request, 'queues/queue_list.html', {'owned_queues': owned_queues})


@login_required
def queue_create(request):
    """Создание новой очереди"""
    log_user_ip(request)
    if request.method == 'POST':
        name = request.POST.get('name')
        description = request.POST.get('description', '')
        
        if not name:
            messages.error(request, 'Название очереди обязательно')
            return render(request, 'queues/queue_form.html')
        
        queue = Queue.objects.create(
            owner=request.user,
            name=name,
            description=description
        )
        messages.success(request, f'Очередь "{name}" создана! Ссылка: {queue.link}')
        return redirect('queues:queue_detail', link=queue.link)
    
    return render(request, 'queues/queue_form.html')


def queue_detail(request, link):
    """Детали очереди по короткой ссылке"""
    queue = get_object_or_404(Queue, link=link)
    
    # Проверка бана
    if request.user.is_authenticated and check_user_banned(request.user, queue):
        messages.error(request, 'Вы забанены в этой очереди')
        return redirect('queues:queue_list')
    
    tickets = Ticket.objects.filter(queue=queue, is_cancelled=False)
    user_ticket = None
    
    if request.user.is_authenticated:
        user_ticket = Ticket.objects.filter(queue=queue, user=request.user, is_cancelled=False).first()
    
    # Получение предложений обмена для текущего пользователя
    exchange_offers = []
    if request.user.is_authenticated and user_ticket:
        exchange_offers = QueueExchange.objects.filter(
            to_ticket=user_ticket,
            status='pending'
        ).select_related('from_ticket__user')
    
    context = {
        'queue': queue,
        'tickets': tickets,
        'user_ticket': user_ticket,
        'is_owner': request.user == queue.owner,
        'exchange_offers': exchange_offers,
    }
    return render(request, 'queues/queue_detail.html', context)


def queue_data_api(request, link):
    """API для получения данных очереди в JSON формате"""
    queue = get_object_or_404(Queue, link=link)
    active_tickets = Ticket.objects.filter(queue=queue, is_cancelled=False).order_by('number')
    
    # Подсчет приблизительного времени ожидания
    AVG_SERVICE_TIME_MINUTES = 2
    
    tickets_data = []
    current_position = 0
    for i, ticket in enumerate(active_tickets):
        position = i + 1
        wait_time_minutes = (position - 1) * AVG_SERVICE_TIME_MINUTES if position > 1 else 0
        
        # Проверка, пора ли уведомлять пользователя (когда остается 3 человека до него)
        if position <= 3 and not ticket.notified and ticket.user:
            ticket.notified = True
            ticket.notified_at = timezone.now()
            ticket.save()
            
            # Создание уведомления
            Notification.objects.create(
                user=ticket.user,
                notification_type='turn',
                message=f'Ваша очередь подошла! Талон {ticket.get_ticket_type_display()}#{ticket.number} в очереди "{queue.name}"',
                ticket=ticket
            )
        
        tickets_data.append({
            'id': ticket.id,
            'number': ticket.number,
            'ticket_type': ticket.get_ticket_type_display(),
            'username': ticket.user.username if ticket.user else 'Аноним',
            'created_at': ticket.created_at.strftime('%d.%m.%Y %H:%M'),
            'position': position,
            'wait_time_minutes': wait_time_minutes,
            'notified': ticket.notified,
        })
        if ticket.user and request.user.is_authenticated and ticket.user == request.user:
            current_position = position
    
    total_active = active_tickets.count()
    
    first_ticket = Ticket.objects.filter(queue=queue).order_by('created_at').first()
    queue_age_minutes = 0
    if first_ticket:
        queue_age_minutes = int((timezone.now() - first_ticket.created_at).total_seconds() / 60)
    
    data = {
        'queue_name': queue.name,
        'is_active': queue.is_active,
        'total_active': total_active,
        'queue_age_minutes': queue_age_minutes,
        'avg_service_time': AVG_SERVICE_TIME_MINUTES,
        'tickets': tickets_data,
        'current_position': current_position,
    }
    
    return JsonResponse(data)


@login_required
def join_queue(request, link):
    """Присоединение к очереди"""
    log_user_ip(request)
    queue = get_object_or_404(Queue, link=link)
    
    if not queue.is_active:
        messages.error(request, 'Эта очередь не активна')
        return redirect('queues:queue_detail', link=link)
    
    # Проверка бана
    if check_user_banned(request.user, queue):
        messages.error(request, 'Вы забанены в этой очереди')
        return redirect('queues:queue_list')
    
    existing_ticket = Ticket.objects.filter(queue=queue, user=request.user, is_cancelled=False).exists()
    if existing_ticket:
        messages.warning(request, 'Вы уже в этой очереди')
        return redirect('queues:queue_detail', link=link)
    
    last_ticket = Ticket.objects.filter(queue=queue).order_by('-number').first()
    next_number = last_ticket.number + 1 if last_ticket else 1
    
    ip_address = get_client_ip(request)
    Ticket.objects.create(
        queue=queue,
        user=request.user,
        number=next_number,
        ticket_type='U',
        ip_address=ip_address
    )
    messages.success(request, f'Вы получили талон У№{next_number}')
    return redirect('queues:queue_detail', link=link)


@login_required
def leave_queue(request, link):
    """Выход из очереди"""
    queue = get_object_or_404(Queue, link=link)
    ticket = Ticket.objects.filter(queue=queue, user=request.user, is_cancelled=False).first()
    
    if ticket:
        ticket.is_cancelled = True
        ticket.save()
        messages.success(request, 'Вы вышли из очереди')
    
    return redirect('queues:queue_detail', link=link)


@login_required
def cancel_ticket(request, ticket_id):
    """Аннулирование талона владельцем очереди или админом"""
    ticket = get_object_or_404(Ticket, id=ticket_id)
    
    if request.user != ticket.queue.owner and not request.user.is_staff:
        messages.error(request, 'Только владелец или админ может аннулировать талоны')
        return redirect('queues:queue_detail', link=ticket.queue.link)
    
    ticket.is_cancelled = True
    ticket.cancelled_by_owner = True
    ticket.save()
    
    # Уведомление пользователя об аннулировании
    if ticket.user:
        Notification.objects.create(
            user=ticket.user,
            notification_type='cancelled',
            message=f'Ваш талон {ticket.get_ticket_type_display()}#{ticket.number} в очереди "{ticket.queue.name}" был аннулирован',
            ticket=ticket
        )
    
    messages.success(request, f'Талон {ticket.get_ticket_type_display()}№{ticket.number} аннулирован')
    return redirect('queues:queue_detail', link=ticket.queue.link)


@login_required
def propose_exchange(request, ticket_id):
    """Предложение обмена местами"""
    if request.method != 'POST':
        return redirect('queues:queue_list')
    
    from_ticket = get_object_or_404(Ticket, id=ticket_id)
    to_ticket_id = request.POST.get('to_ticket')
    message = request.POST.get('message', '')
    
    if not to_ticket_id:
        messages.error(request, 'Не выбран талон для обмена')
        return redirect('queues:queue_detail', link=from_ticket.queue.link)
    
    to_ticket = get_object_or_404(Ticket, id=to_ticket_id)
    
    if from_ticket.queue != to_ticket.queue:
        messages.error(request, 'Обмен возможен только в пределах одной очереди')
        return redirect('queues:queue_detail', link=from_ticket.queue.link)
    
    if from_ticket.user != request.user:
        messages.error(request, 'Вы можете предложить обмен только со своего талона')
        return redirect('queues:queue_detail', link=from_ticket.queue.link)
    
    if from_ticket.number <= to_ticket.number:
        messages.error(request, 'Можно предложить обмен только на место позади вас')
        return redirect('queues:queue_detail', link=from_ticket.queue.link)
    
    # Проверка количества полученных предложений для получателя
    pending_offers_count = QueueExchange.objects.filter(
        to_ticket=to_ticket,
        status='pending'
    ).count()
    
    if pending_offers_count >= 5:
        messages.error(request, 'Пользователь уже получил максимальное количество предложений (5)')
        return redirect('queues:queue_detail', link=from_ticket.queue.link)
    
    # Создание предложения обмена
    QueueExchange.objects.create(
        from_ticket=from_ticket,
        to_ticket=to_ticket,
        message=message
    )
    
    # Уведомление получателя
    if to_ticket.user:
        Notification.objects.create(
            user=to_ticket.user,
            notification_type='exchange',
            message=f'Пользователь {from_ticket.user.username} предлагает обмен местами: {from_ticket.get_ticket_type_display()}#{from_ticket.number} на ваш {to_ticket.get_ticket_type_display()}#{to_ticket.number}',
            ticket=to_ticket
        )
    
    messages.success(request, 'Предложение обмена отправлено')
    return redirect('queues:queue_detail', link=from_ticket.queue.link)


@login_required
def respond_exchange(request, exchange_id):
    """Ответ на предложение обмена (принять/отклонить)"""
    exchange = get_object_or_404(QueueExchange, id=exchange_id)
    
    if exchange.to_ticket.user != request.user:
        messages.error(request, 'Это предложение не адресовано вам')
        return redirect('queues:queue_list')
    
    action = request.POST.get('action')
    response_message = request.POST.get('response_message', '')
    
    if action == 'accept':
        # Обмен местами
        old_from_number = exchange.from_ticket.number
        old_to_number = exchange.to_ticket.number
        
        exchange.from_ticket.number = old_to_number
        exchange.to_ticket.number = old_from_number
        exchange.from_ticket.save()
        exchange.to_ticket.save()
        
        exchange.status = 'accepted'
        exchange.save()
        
        # Уведомления
        Notification.objects.create(
            user=exchange.from_ticket.user,
            notification_type='exchange_accepted',
            message=f'Ваш обмен на талон {exchange.to_ticket.get_ticket_type_display()}#{exchange.to_ticket.number} принят!',
            exchange=exchange
        )
        if response_message:
            Notification.objects.create(
                user=exchange.from_ticket.user,
                notification_type='exchange_accepted',
                message=f'Сообщение от {exchange.to_ticket.user.username}: {response_message}',
                exchange=exchange
            )
        
        messages.success(request, 'Обмен выполнен успешно')
        
    elif action == 'decline':
        exchange.status = 'declined'
        exchange.save()
        
        # Уведомление отправителя
        if exchange.from_ticket.user:
            Notification.objects.create(
                user=exchange.from_ticket.user,
                notification_type='exchange_declined',
                message=f'Ваше предложение обмена отклонено',
                exchange=exchange
            )
            if response_message:
                Notification.objects.create(
                    user=exchange.from_ticket.user,
                    notification_type='exchange_declined',
                    message=f'Сообщение от {exchange.to_ticket.user.username}: {response_message}',
                    exchange=exchange
                )
        
        messages.info(request, 'Предложение отклонено')
    
    return redirect('queues:queue_detail', link=exchange.from_ticket.queue.link)


@login_required
def ban_user(request, queue_link):
    """Бан пользователя в очереди"""
    if request.method != 'POST':
        return redirect('queues:queue_list')
    
    queue = get_object_or_404(Queue, link=queue_link)
    
    if request.user != queue.owner and not request.user.is_staff:
        messages.error(request, 'Только владелец или админ может банить пользователей')
        return redirect('queues:queue_detail', link=queue_link)
    
    user_id = request.POST.get('user_id')
    scope = request.POST.get('scope', 'queue')
    reason = request.POST.get('reason', '')
    
    if not user_id:
        messages.error(request, 'Не указан пользователь')
        return redirect('queues:queue_detail', link=queue_link)
    
    banned_user = get_object_or_404(User, id=user_id)
    
    # Отмена всех активных талонов пользователя в этой очереди
    Ticket.objects.filter(queue=queue, user=banned_user, is_cancelled=False).update(
        is_cancelled=True,
        cancelled_by_owner=True
    )
    
    # Создание бана
    Ban.objects.create(
        queue=queue,
        user=banned_user,
        scope=scope,
        reason=reason,
        created_by=request.user
    )
    
    # Уведомление забаненного
    Notification.objects.create(
        user=banned_user,
        notification_type='banned',
        message=f'Вы забанены в очереди "{queue.name}" ({scope}): {reason}' if reason else f'Вы забанены в очереди "{queue.name}" ({scope})',
    )
    
    messages.success(request, f'Пользователь {banned_user.username} забанен')
    return redirect('queues:queue_detail', link=queue_link)


@login_required
def notifications_list(request):
    """Список уведомлений пользователя"""
    notifications = Notification.objects.filter(user=request.user).order_by('-created_at')
    unread_count = notifications.filter(is_read=False).count()
    
    # Помечаем все как прочитанные
    notifications.update(is_read=True)
    
    return render(request, 'queues/notifications.html', {
        'notifications': notifications,
        'unread_count': unread_count
    })


@login_required
def terminal_register(request):
    """Регистрация терминала пользователем"""
    if request.method == 'POST':
        name = request.POST.get('name')
        if not name:
            messages.error(request, 'Название терминала обязательно')
            return render(request, 'queues/terminal_register.html')
        
        terminal = Terminal.objects.create(
            owner=request.user,
            name=name
        )
        messages.success(request, f'Терминал зарегистрирован. Секретный хэш: {terminal.hash_secret}')
        return redirect('queues:terminal_list')
    
    return render(request, 'queues/terminal_register.html')


@login_required
def terminal_list(request):
    """Список терминалов пользователя"""
    terminals = Terminal.objects.filter(owner=request.user)
    return render(request, 'queues/terminal_list.html', {'terminals': terminals})


@csrf_exempt
def terminal_get_ticket(request):
    """API для получения анонимного талона с терминала"""
    if request.method != 'POST':
        return JsonResponse({'error': 'Method not allowed'}, status=405)
    
    try:
        data = json.loads(request.body)
        link = data.get('link')
        terminal_hash = data.get('terminal_hash')
        
        if not link or not terminal_hash:
            return JsonResponse({'error': 'Missing link or terminal_hash'}, status=400)
        
        # Проверка терминала
        try:
            terminal = Terminal.objects.get(hash_secret=terminal_hash)
        except Terminal.DoesNotExist:
            return JsonResponse({'error': 'Invalid terminal hash'}, status=403)
        
        if not terminal.is_active:
            return JsonResponse({'error': 'Terminal is not active'}, status=403)
        
        # Проверка очереди
        queue = get_object_or_404(Queue, link=link)
        
        # Проверка прав терминала
        if not terminal.can_serve_queue(queue):
            return JsonResponse({'error': 'Terminal cannot serve this queue'}, status=403)
        
        if not queue.is_active:
            return JsonResponse({'error': 'Queue is not active'}, status=400)
        
        # Получение следующего номера
        last_ticket = Ticket.objects.filter(queue=queue).order_by('-number').first()
        next_number = last_ticket.number + 1 if last_ticket else 1
        
        # Создание анонимного талона (без подтверждения печати)
        ticket = Ticket.objects.create(
            queue=queue,
            user=None,
            number=next_number,
            ticket_type='T',
            is_anonymous=True,
            printed=False,
            print_confirmed=False
        )
        
        return JsonResponse({
            'success': True,
            'ticket_number': next_number,
            'ticket_id': ticket.id,
            'ticket_type': 'T',
            'queue_name': queue.name,
            'print_confirmation_required': True,
            'message': 'Ticket created. Please confirm printing.'
        })
        
    except json.JSONDecodeError:
        return JsonResponse({'error': 'Invalid JSON'}, status=400)
    except Exception as e:
        return JsonResponse({'error': str(e)}, status=500)


@csrf_exempt
def terminal_confirm_print(request):
    """API для подтверждения печати талона"""
    if request.method != 'POST':
        return JsonResponse({'error': 'Method not allowed'}, status=405)
    
    try:
        data = json.loads(request.body)
        ticket_id = data.get('ticket_id')
        terminal_hash = data.get('terminal_hash')
        
        if not ticket_id or not terminal_hash:
            return JsonResponse({'error': 'Missing ticket_id or terminal_hash'}, status=400)
        
        # Проверка терминала
        try:
            terminal = Terminal.objects.get(hash_secret=terminal_hash)
        except Terminal.DoesNotExist:
            return JsonResponse({'error': 'Invalid terminal hash'}, status=403)
        
        # Получение талона
        ticket = get_object_or_404(Ticket, id=ticket_id)
        
        if ticket.ticket_type != 'T':
            return JsonResponse({'error': 'Not a terminal ticket'}, status=400)
        
        if ticket.print_confirmed:
            return JsonResponse({'error': 'Print already confirmed'}, status=400)
        
        # Подтверждение печати
        ticket.printed = True
        ticket.print_confirmed = True
        ticket.save()
        
        return JsonResponse({
            'success': True,
            'message': 'Print confirmed. Ticket is now active in queue.',
            'ticket_number': ticket.number,
            'position': Ticket.objects.filter(
                queue=ticket.queue,
                is_cancelled=False,
                number__lte=ticket.number
            ).count()
        })
        
    except json.JSONDecodeError:
        return JsonResponse({'error': 'Invalid JSON'}, status=400)
    except Exception as e:
        return JsonResponse({'error': str(e)}, status=500)


@login_required
def queue_participants_list(request, link):
    """Список людей в очереди (активные)"""
    queue = get_object_or_404(Queue, link=link)
    
    if request.user != queue.owner and not request.user.is_staff:
        messages.error(request, 'Только владелец или админ может просматривать списки')
        return redirect('queues:queue_list')
    
    active_tickets = Ticket.objects.filter(
        queue=queue,
        is_cancelled=False
    ).select_related('user').order_by('number')
    
    return render(request, 'queues/participants_list.html', {
        'queue': queue,
        'active_tickets': active_tickets,
        'list_type': 'active'
    })


@login_required
def queue_finished_list(request, link):
    """Список людей, отсидевших в очереди (аннулированные/вышедшие)"""
    queue = get_object_or_404(Queue, link=link)
    
    if request.user != queue.owner and not request.user.is_staff:
        messages.error(request, 'Только владелец или админ может просматривать списки')
        return redirect('queues:queue_list')
    
    finished_tickets = Ticket.objects.filter(
        queue=queue,
        is_cancelled=True
    ).select_related('user').order_by('-created_at')
    
    return render(request, 'queues/participants_list.html', {
        'queue': queue,
        'finished_tickets': finished_tickets,
        'list_type': 'finished'
    })


def verify_ticket(request, ticket_hash):
    """Проверка талона по хешу"""
    try:
        # Ищем активный талон
        ticket = Ticket.objects.filter(ticket_hash=ticket_hash).first()
        
        if not ticket:
            # Ищем в завершенных
            from .models import CompletedTicket
            ticket = CompletedTicket.objects.filter(ticket_hash=ticket_hash).first()
            if ticket:
                return render(request, 'queues/verify_ticket.html', {
                    'ticket': ticket,
                    'is_completed': True,
                    'valid': True
                })
            return render(request, 'queues/verify_ticket.html', {
                'valid': False,
                'error': 'Талон не найден'
            })
        
        # Проверка целостности хеша
        if not ticket.verify_hash():
            return render(request, 'queues/verify_ticket.html', {
                'ticket': ticket,
                'valid': False,
                'error': 'Неверный хеш талона'
            })
        
        return render(request, 'queues/verify_ticket.html', {
            'ticket': ticket,
            'is_completed': False,
            'valid': True
        })
        
    except Exception as e:
        return render(request, 'queues/verify_ticket.html', {
            'valid': False,
            'error': f'Ошибка проверки: {str(e)}'
        })


@login_required
def delete_ticket(request, ticket_id):
    """Удаление талона владельцем очереди или админом"""
    ticket = get_object_or_404(Ticket, id=ticket_id)
    
    if request.user != ticket.queue.owner and not request.user.is_staff:
        messages.error(request, 'Только владелец или админ может удалять талоны')
        return redirect('queues:queue_detail', link=ticket.queue.link)
    
    # Перемещаем в завершенные перед удалением
    from .models import CompletedTicket
    CompletedTicket.objects.create(
        queue=ticket.queue,
        user=ticket.user,
        number=ticket.number,
        ticket_type=ticket.ticket_type,
        created_at=ticket.created_at,
        ip_address=ticket.ip_address,
        ticket_hash=ticket.ticket_hash
    )
    
    ticket.delete()
    
    messages.success(request, f'Талон {ticket.get_ticket_type_display()}№{ticket.number} удален')
    return redirect('queues:queue_detail', link=ticket.queue.link)


@login_required
def export_participants_csv(request, link):
    """Экспорт списка участников очереди в CSV"""
    import csv
    from django.http import HttpResponse
    
    queue = get_object_or_404(Queue, link=link)
    
    if request.user != queue.owner and not request.user.is_staff:
        messages.error(request, 'Только владелец или админ может экспортировать списки')
        return redirect('queues:queue_list')
    
    list_type = request.GET.get('type', 'active')
    
    if list_type == 'active':
        tickets = Ticket.objects.filter(
            queue=queue,
            is_cancelled=False
        ).select_related('user').order_by('number')
    else:
        tickets = Ticket.objects.filter(
            queue=queue,
            is_cancelled=True
        ).select_related('user').order_by('-created_at')
    
    response = HttpResponse(content_type='text/csv')
    filename = f'queue_{queue.link}_{list_type}.csv'
    response['Content-Disposition'] = f'attachment; filename="{filename}"'
    
    writer = csv.writer(response)
    writer.writerow(['Номер', 'Тип', 'Пользователь', 'Дата создания', 'IP адрес', 'Статус'])
    
    for ticket in tickets:
        writer.writerow([
            ticket.number,
            ticket.get_ticket_type_display(),
            ticket.user.username if ticket.user else 'Аноним',
            ticket.created_at.strftime('%d.%m.%Y %H:%M'),
            ticket.ip_address or '',
            'Активен' if not ticket.is_cancelled else 'Отменен'
        ])
    
    return response

