from django.shortcuts import render, redirect
from django.contrib.auth import login, logout, authenticate
from django.contrib.auth.decorators import login_required
from django.contrib.auth.models import User
from django.contrib import messages


def login_view(request):
    """Вход пользователя"""
    if request.method == 'POST':
        username = request.POST.get('username')
        password = request.POST.get('password')
        user = authenticate(request, username=username, password=password)
        if user:
            login(request, user)
            return redirect('queues:queue_list')
        messages.error(request, 'Неверное имя пользователя или пароль')
    return render(request, 'accounts/login.html')


def register_view(request):
    """Регистрация пользователя"""
    if request.method == 'POST':
        username = request.POST.get('username')
        password = request.POST.get('password')
        password_confirm = request.POST.get('password_confirm')
        
        if password != password_confirm:
            messages.error(request, 'Пароли не совпадают')
            return render(request, 'accounts/register.html')
        
        if User.objects.filter(username=username).exists():
            messages.error(request, 'Пользователь с таким именем уже существует')
            return render(request, 'accounts/register.html')
        
        user = User.objects.create_user(username=username, password=password)
        login(request, user)
        return redirect('queues:queue_list')
    
    return render(request, 'accounts/register.html')


@login_required
def logout_view(request):
    """Выход пользователя"""
    logout(request)
    return redirect('accounts:login')

