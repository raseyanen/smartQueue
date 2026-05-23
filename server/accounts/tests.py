import pytest
from django.urls import reverse
from django.contrib.auth.models import User
from .models import Profile


@pytest.mark.django_db
class TestRegistration:
    """Тесты для регистрации пользователей"""
    
    def test_register_page_loads(self, client):
        """Страница регистрации загружается корректно"""
        response = client.get(reverse('accounts:register'))
        assert response.status_code == 200
        assert 'accounts/register.html' in [t.name for t in response.templates]
    
    def test_user_registration(self, client):
        """Регистрация нового пользователя"""
        data = {
            'username': 'testuser',
            'password': 'testpass123',
            'password_confirm': 'testpass123',
        }
        response = client.post(reverse('accounts:register'), data)
        
        # После успешной регистрации происходит редирект
        assert response.status_code == 302
        
        # Проверяем, что пользователь создан
        user = User.objects.filter(username='testuser').first()
        assert user is not None
        
        # Проверяем, что профиль создан автоматически
        assert hasattr(user, 'profile')
    
    def test_login_page_loads(self, client):
        """Страница входа загружается корректно"""
        response = client.get(reverse('accounts:login'))
        assert response.status_code == 200
    
    def test_user_login(self, client):
        """Вход пользователя в систему"""
        # Создаем пользователя
        user = User.objects.create_user(username='loginuser', password='pass123')
        
        data = {
            'username': 'loginuser',
            'password': 'pass123',
        }
        response = client.post(reverse('accounts:login'), data)
        
        # После входа происходит редирект
        assert response.status_code == 302
        
        # Проверяем, что пользователь аутентифицирован
        assert '_auth_user_id' in client.session


@pytest.mark.django_db
class TestProfile:
    """Тесты для профиля пользователя"""
    
    def test_profile_creation_on_user_create(self):
        """Профиль создается автоматически при создании пользователя"""
        user = User.objects.create_user(username='profileuser', password='pass123')
        assert hasattr(user, 'profile')
        assert user.profile.user == user
    
    def test_profile_str_representation(self):
        """Проверка строкового представления профиля"""
        user = User.objects.create_user(username='struser', password='pass123')
        profile = user.profile
        assert str(profile) == f"Profile: {user.username}"
