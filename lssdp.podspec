Pod::Spec.new do |s|
  s.name             = "lssdp"
  s.version          = "1.0.0"
  s.summary          = "The open source lssdp library."
  s.homepage         = "https://github.com/commendinternational/lssdp"
  s.license          = 'Code is MIT.'
  s.author           = "yodakohl" 
  s.source           = { :git => "https://github.com/commendinternational/lssdp.git" , :tag => 'master' }
  s.social_media_url = 'https://twitter.com/artsy'

  s.platform     = :ios, '9.0'
  s.requires_arc = true

  s.source_files = 'lssdp.c'
  s.source_files = 'lssdp.h'

end