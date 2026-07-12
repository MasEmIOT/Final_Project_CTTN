import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

// base './' de build ra file tinh chay duoc ca khi nhung tren gateway lan APK (file://)
export default defineConfig({
  plugins: [react()],
  base: './',
  server: { host: true, port: 5173 },
  build: { outDir: 'dist', chunkSizeWarningLimit: 1500 },
})
