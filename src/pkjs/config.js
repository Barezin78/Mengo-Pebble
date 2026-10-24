// Clay settings page (shown via the gear icon in the Pebble phone app)
module.exports = [
  { type: 'heading', defaultValue: 'Mengão' },
  { type: 'text', defaultValue: 'Configurações do mostrador rubro-negro.' },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Estilo' },
      {
        type: 'radiogroup', messageKey: 'CFG_STYLE', label: 'Visual do mostrador', defaultValue: '0',
        options: [
          { label: 'Clássico (listrado)', value: '0' },
          { label: 'Minimalista', value: '1' },
          { label: 'Placar (estádio)', value: '2' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Clima' },
      { type: 'toggle', messageKey: 'CFG_SHOW_WEATHER', label: 'Mostrar o clima', defaultValue: true },
      {
        type: 'radiogroup', messageKey: 'CFG_FAHRENHEIT', label: 'Unidade de temperatura', defaultValue: '0',
        options: [ { label: 'Celsius (°C)', value: '0' }, { label: 'Fahrenheit (°F)', value: '1' } ]
      },
      {
        type: 'select', messageKey: 'CFG_WX_INTERVAL', label: 'Atualizar o clima a cada', defaultValue: '30',
        options: [
          { label: '15 minutos', value: '15' },
          { label: '30 minutos', value: '30' },
          { label: '1 hora', value: '60' },
          { label: '2 horas', value: '120' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Jogos do Flamengo' },
      { type: 'toggle', messageKey: 'CFG_SHOW_MATCH', label: 'Mostrar o próximo jogo e o placar ao vivo', defaultValue: true },
      { type: 'toggle', messageKey: 'CFG_VIBE_GOAL', label: 'Vibrar no gol do Flamengo', defaultValue: true },
      { type: 'text', defaultValue: 'Durante o jogo o placar é atualizado a cada minuto. Fonte: ESPN.' }
    ]
  },
  {
    type: 'section',
    items: [
      { type: 'heading', defaultValue: 'Conexão' },
      { type: 'toggle', messageKey: 'CFG_VIBE_BT', label: 'Vibrar quando o celular desconectar', defaultValue: true }
    ]
  },
  { type: 'submit', defaultValue: 'Salvar' }
];
