import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/user': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
      '/profile': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
      '/cards': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
      '/decks': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
      '/matchfind': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
      '/battleroom': {
        target: 'http://127.0.0.1:5555',
        changeOrigin: true,
      },
    },
  },
});
